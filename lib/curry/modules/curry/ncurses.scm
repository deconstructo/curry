;;; (curry ncurses) — terminal UI via ncurses, pure Scheme + FFI.
;;;
;;; Requires ncurses installed on the *running* system (dlopen'd at import
;;; time via (curry ffi), not linked at build time — no CMake flag needed,
;;; just BUILD_FFI=ON). macOS and most Linux distros ship it already;
;;; otherwise: `apt install libncurses-dev` (Debian/Ubuntu),
;;; `dnf install ncurses-devel` (Fedora/RHEL), `brew install ncurses` (macOS,
;;; if you want a newer one than the system copy).
;;;
;;; Design: one idiomatic layer, not a raw 1:1 transliteration of the C API
;;; (that's what CHICKEN's ncurses egg already is, and it leaves every
;;; C-ism — manual attron/attroff pairing, positional window args on every
;;; call, magic-number key codes — for the caller to deal with). Windows are
;;; records, not raw pointers; attributes apply via with-attributes rather
;;; than manual on/off pairing; getch translates known key codes to plain
;;; symbols ('up 'down 'enter ...) while still returning a raw integer for
;;; anything it doesn't recognize. There is no separate "raw bindings" escape
;;; hatch exported here — keeping this module to one clean surface is a
;;; deliberate simplicity choice, not an oversight; the underlying %nc-*
;;; foreign bindings are private.
;;;
;;; The printw/wprintw/mvprintw family (variadic, printf-style) is
;;; deliberately not bound — (curry ffi) has no variadic-call support, and
;;; ncurses-add-string! plus Scheme's own string-append/number->string cover
;;; the same ground without needing it.
;;;
;;; Two known, unavoidable rough edges from the underlying C library, not
;;; this module's own design:
;;;   - initscr() is documented to abort the process outright on a fatal
;;;     terminal-setup failure (no TERM, bad terminfo) rather than return an
;;;     error Scheme could catch — there's no way around this from userland.
;;;   - Attribute bit values (A_BOLD etc.) and the color-pair packing scheme
;;;     ncurses uses internally are technically a build-time constant of
;;;     whatever ncurses was compiled with (NCURSES_ATTR_SHIFT). The values
;;;     here assume the near-universal convention (shift = 8) essentially
;;;     every non-C ncurses binding relies on; color pairs are set via
;;;     wcolor_set (a real function taking a pair *number* directly) rather
;;;     than the COLOR_PAIR(n) macro specifically to avoid needing to know
;;;     that packing scheme at all.
;;;
;;; API:
;;;   (ncurses-init!) -> <ncurses-window>          [the main/"stdscr" window]
;;;   (ncurses-end!)
;;;   (call-with-ncurses proc)                     [proc receives the main window; endwin always runs]
;;;   (ncurses-window-new h w y x) -> <ncurses-window>
;;;   (ncurses-window-delete! win)
;;;   (ncurses-window? x)
;;;   (ncurses-move! win y x)
;;;   (ncurses-add-string! win str) / (ncurses-add-string! win y x str)
;;;   (ncurses-refresh! win)
;;;   (ncurses-clear! win) / (ncurses-erase! win)
;;;   (ncurses-box! win)
;;;   (ncurses-window-height win) / (ncurses-window-width win)
;;;   (ncurses-getch win) -> symbol | char | integer
;;;   (ncurses-attr-on! win attr) / (ncurses-attr-off! win attr) / (ncurses-attr-set! win attr)
;;;   (with-attributes win attr body ...)
;;;   a-normal a-bold a-underline a-reverse a-blink a-dim a-standout
;;;   a-altcharset a-invis a-protect a-italic
;;;   (ncurses-start-color!) -> boolean
;;;   (ncurses-init-color-pair! pair fg bg)        [fg/bg: 'black/'red/'green/'yellow/'blue/'magenta/'cyan/'white or 0-7]
;;;   (ncurses-set-color! win pair)

(define-library (curry ncurses)
  (import (curry ffi))
  (import (scheme base))
  (export
    ncurses-init! ncurses-end! call-with-ncurses
    ncurses-window? ncurses-window-new ncurses-window-delete!
    ncurses-move! ncurses-add-string! ncurses-refresh!
    ncurses-clear! ncurses-erase! ncurses-box!
    ncurses-window-height ncurses-window-width
    ncurses-getch
    ncurses-attr-on! ncurses-attr-off! ncurses-attr-set!
    with-attributes
    a-normal a-bold a-underline a-reverse a-blink a-dim a-standout
    a-altcharset a-invis a-protect a-italic
    ncurses-start-color! ncurses-init-color-pair! ncurses-set-color!)
  (begin

;; ── Library discovery ────────────────────────────────────────────────────────
;;
;; Prefer the wide-char build (ncursesw) — it handles UTF-8 correctly, which
;; matters given curry's own Unicode-heavy reader — falling back to plain
;; ncurses. Every symbol this module binds has an identical name/signature
;; in both builds (only the wide-specific *_wch-family functions differ,
;; none of which this module uses), so no binding changes based on which
;; one actually loaded.

(define %ncurses-candidates
  (list
    "libncursesw.dylib" "libncurses.dylib"                          ; macOS, on loader path
    "libncursesw.so.6" "libncurses.so.6"                             ; Linux, on loader path
    "/opt/homebrew/opt/ncurses/lib/libncursesw.dylib"                ; Homebrew, Apple Silicon
    "/usr/local/opt/ncurses/lib/libncursesw.dylib"                   ; Homebrew, Intel Mac
    "/usr/lib/x86_64-linux-gnu/libncursesw.so.6"                     ; Debian/Ubuntu, x86_64
    "/usr/lib/aarch64-linux-gnu/libncursesw.so.6"                    ; Debian/Ubuntu, arm64
    "/usr/lib64/libncursesw.so.6"))                                  ; Fedora/RHEL

(define (%ncurses-try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

(define %nc-lib
  (or (%ncurses-try-load %ncurses-candidates)
      (error "ncurses: could not load libncurses — install it first:
  macOS:           already installed system-wide, or `brew install ncurses` for a newer one
  Debian/Ubuntu:   apt install libncurses-dev
  Fedora/RHEL:     dnf install ncurses-devel")))

;; ── Attribute constants ──────────────────────────────────────────────────────
;;
;; NCURSES_BITS(mask, shift) = mask << (shift + NCURSES_ATTR_SHIFT), with
;; NCURSES_ATTR_SHIFT = 8 — the standard layout, confirmed against a real
;; system curses.h and assumed the same way every other FFI-based ncurses
;; binding (Python ctypes, Go, etc.) assumes it, since there's no runtime
;; function that reports this shift amount to query it instead.

(define a-normal      0)
(define a-standout    (expt 2 16))
(define a-underline   (expt 2 17))
(define a-reverse     (expt 2 18))
(define a-blink       (expt 2 19))
(define a-dim         (expt 2 20))
(define a-bold        (expt 2 21))
(define a-altcharset  (expt 2 22))
(define a-invis       (expt 2 23))
(define a-protect     (expt 2 24))
(define a-italic      (expt 2 31)) ; ncurses extension, not present in every build

;; ── Raw foreign bindings (private) ───────────────────────────────────────────

(define-foreign (%nc-initscr) → c-ptr #:from %nc-lib #:c-name "initscr")
(define-foreign (%nc-endwin) → int #:from %nc-lib #:c-name "endwin")
(define-foreign (%nc-newwin (nlines int) (ncols int) (begin_y int) (begin_x int))
  → c-ptr #:from %nc-lib #:c-name "newwin")
(define-foreign (%nc-delwin (win c-ptr)) → int #:from %nc-lib #:c-name "delwin")
(define-foreign (%nc-wrefresh (win c-ptr)) → int #:from %nc-lib #:c-name "wrefresh")
(define-foreign (%nc-wgetch (win c-ptr)) → int #:from %nc-lib #:c-name "wgetch")
(define-foreign (%nc-waddstr (win c-ptr) (str c-string)) → int #:from %nc-lib #:c-name "waddstr")
(define-foreign (%nc-wmove (win c-ptr) (y int) (x int)) → int #:from %nc-lib #:c-name "wmove")
(define-foreign (%nc-wattron (win c-ptr) (attrs int)) → int #:from %nc-lib #:c-name "wattron")
(define-foreign (%nc-wattroff (win c-ptr) (attrs int)) → int #:from %nc-lib #:c-name "wattroff")
(define-foreign (%nc-wattrset (win c-ptr) (attrs int)) → int #:from %nc-lib #:c-name "wattrset")
(define-foreign (%nc-wcolor-set (win c-ptr) (pair int) (opts c-ptr)) → int #:from %nc-lib #:c-name "wcolor_set")
(define-foreign (%nc-start-color) → int #:from %nc-lib #:c-name "start_color")
(define-foreign (%nc-init-pair (pair int) (fg int) (bg int)) → int #:from %nc-lib #:c-name "init_pair")
(define-foreign (%nc-cbreak) → int #:from %nc-lib #:c-name "cbreak")
(define-foreign (%nc-noecho) → int #:from %nc-lib #:c-name "noecho")
(define-foreign (%nc-keypad (win c-ptr) (bf int)) → int #:from %nc-lib #:c-name "keypad")
(define-foreign (%nc-werase (win c-ptr)) → int #:from %nc-lib #:c-name "werase")
(define-foreign (%nc-wclear (win c-ptr)) → int #:from %nc-lib #:c-name "wclear")
(define-foreign (%nc-box (win c-ptr) (verch int) (horch int)) → int #:from %nc-lib #:c-name "box")
(define-foreign (%nc-getmaxx (win c-ptr)) → int #:from %nc-lib #:c-name "getmaxx")
(define-foreign (%nc-getmaxy (win c-ptr)) → int #:from %nc-lib #:c-name "getmaxy")

;; ── Window record ─────────────────────────────────────────────────────────────

(define-record-type <ncurses-window>
  (%make-ncurses-window cptr)
  ncurses-window?
  (cptr ncurses-window-cptr))

;; ── Session ───────────────────────────────────────────────────────────────────
;;
;; Applies the boilerplate essentially every curses program wants
;; immediately (cbreak, noecho, keypad-enabled arrow/function keys) rather
;; than making every caller repeat it — this is the concrete meaning of
;; "developer friendly" for this module. Raw toggles remain available
;; (there is currently no exported echo!/nocbreak! — add them if a program
;; genuinely needs to turn this back off; keeping the export list small for
;; now per the "keep it simple" brief).

(define (ncurses-init!)
  (let ((scr (%nc-initscr)))
    (when (cptr-null? scr)
      (error "ncurses-init!: initscr() returned NULL"))
    (%nc-cbreak)
    (%nc-noecho)
    (%nc-keypad scr 1)
    (%make-ncurses-window scr)))

(define (ncurses-end!) (%nc-endwin))

;; Runs (proc main-window); endwin always runs afterward, including when
;; proc raises — the single most common ncurses program bug is leaving the
;; terminal in raw/no-echo mode after a crash, and this is the one thing
;; worth guaranteeing rather than leaving to every caller to remember.
(define (call-with-ncurses proc)
  (let ((win (ncurses-init!)))
    (dynamic-wind
      (lambda () #f)
      (lambda () (proc win))
      (lambda () (ncurses-end!)))))

;; ── Windows ───────────────────────────────────────────────────────────────────

(define (ncurses-window-new h w y x)
  (let ((cptr (%nc-newwin h w y x)))
    (when (cptr-null? cptr)
      (error "ncurses-window-new: newwin() failed (bad dimensions/position?)" h w y x))
    (%make-ncurses-window cptr)))

(define (ncurses-window-delete! win) (%nc-delwin (ncurses-window-cptr win)))

(define (ncurses-move! win y x) (%nc-wmove (ncurses-window-cptr win) y x))

;; (ncurses-add-string! win str) or (ncurses-add-string! win y x str)
;;
;; Both arities are validated by type, not just arity — (curry ffi)'s
;; c-string marshaling silently turns a non-string argument into a NULL
;; pointer rather than raising, which would otherwise make a call like
;; (ncurses-add-string! win 5 6 42) reach waddstr(win, NULL) as undefined
;; behavior in ncurses (typically a segfault) instead of a catchable
;; Scheme error; a call with the wrong types in the y/x position would
;; similarly marshal to 0 and silently move the cursor to the wrong place.
(define (ncurses-add-string! win . rest)
  (cond
    ((and (pair? rest) (null? (cdr rest)) (string? (car rest)))
     (%nc-waddstr (ncurses-window-cptr win) (car rest)))
    ((and (= (length rest) 3)
          (integer? (car rest)) (integer? (cadr rest)) (string? (caddr rest)))
     (%nc-wmove (ncurses-window-cptr win) (car rest) (cadr rest))
     (%nc-waddstr (ncurses-window-cptr win) (caddr rest)))
    (else (error "ncurses-add-string!: expected (win str) or (win y x str)" rest))))

(define (ncurses-refresh! win) (%nc-wrefresh (ncurses-window-cptr win)))
(define (ncurses-clear! win) (%nc-wclear (ncurses-window-cptr win)))
(define (ncurses-erase! win) (%nc-werase (ncurses-window-cptr win)))

;; verch/horch 0 tells ncurses to use its default line-drawing characters.
(define (ncurses-box! win) (%nc-box (ncurses-window-cptr win) 0 0))

(define (ncurses-window-height win) (%nc-getmaxy (ncurses-window-cptr win)))
(define (ncurses-window-width win) (%nc-getmaxx (ncurses-window-cptr win)))

;; ── Input ─────────────────────────────────────────────────────────────────────
;;
;; KEY_* values below are from a real curses.h, not guessed: DOWN=258
;; UP=259 LEFT=260 RIGHT=261 HOME=262 BACKSPACE=263 F1..F4=265..268
;; DC=330 IC=331 NPAGE=338 PPAGE=339 ENTER=343 END=360.

(define %nc-key-table
  (list (cons 258 'down) (cons 259 'up) (cons 260 'left) (cons 261 'right)
        (cons 262 'home) (cons 263 'backspace)
        (cons 265 'f1) (cons 266 'f2) (cons 267 'f3) (cons 268 'f4)
        (cons 330 'delete) (cons 331 'insert)
        (cons 338 'page-down) (cons 339 'page-up)
        (cons 343 'enter) (cons 360 'end)))

;; Returns a symbol for a recognized special key, a character for a plain
;; printable/ASCII code, or the raw integer for anything else (including
;; ERR = -1, e.g. from a nodelay window with no input ready).
(define (ncurses-getch win)
  (let* ((code (%nc-wgetch (ncurses-window-cptr win)))
         (known (assv code %nc-key-table)))
    (cond
      (known (cdr known))
      ((and (>= code 0) (< code 256)) (integer->char code))
      (else code))))

;; ── Attributes ────────────────────────────────────────────────────────────────

(define (ncurses-attr-on! win attr) (%nc-wattron (ncurses-window-cptr win) attr))
(define (ncurses-attr-off! win attr) (%nc-wattroff (ncurses-window-cptr win) attr))
(define (ncurses-attr-set! win attr) (%nc-wattrset (ncurses-window-cptr win) attr))

;; (with-attributes win (bitwise-or a-bold a-underline) body ...) — applies
;; attr for body, then always turns it back off, even if body raises.
(define-syntax with-attributes
  (syntax-rules ()
    ((_ win attr body ...)
     (let ((w win) (a attr))
       (dynamic-wind
         (lambda () (ncurses-attr-on! w a))
         (lambda () body ...)
         (lambda () (ncurses-attr-off! w a)))))))

;; ── Colors ────────────────────────────────────────────────────────────────────

(define %nc-color-table
  '((black . 0) (red . 1) (green . 2) (yellow . 3)
    (blue . 4) (magenta . 5) (cyan . 6) (white . 7)))

(define (%nc-color->int c)
  (if (integer? c)
      c
      (let ((p (assq c %nc-color-table)))
        (if p (cdr p) (error "ncurses: unknown color" c)))))

;; #t if the terminal supports color and color mode is now active, #f
;; otherwise — start_color()'s own return code (OK=0/ERR=-1) is used rather
;; than calling has_colors() separately, since has_colors() returns
;; ncurses' narrow `bool` C type, which (curry ffi) has no exact-width
;; return marshaling for; start_color()'s plain `int` return has no such
;; ambiguity.
(define (ncurses-start-color!) (= (%nc-start-color) 0))

(define (ncurses-init-color-pair! pair fg bg)
  (%nc-init-pair pair (%nc-color->int fg) (%nc-color->int bg)))

(define (ncurses-set-color! win pair)
  (%nc-wcolor-set (ncurses-window-cptr win) pair (cptr-null)))

  )) ;; end begin, define-library
