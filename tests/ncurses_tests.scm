;;; (curry ncurses) tests — import, symbol resolution, and pure-Scheme
;;; constants/logic only. No initscr()/newwin()/getch() here: those need a
;;; real controlling terminal, which a CI runner typically doesn't have, and
;;; ncurses' own documented behavior on a broken terminal setup is to abort
;;; the process outright — not something a test suite can safely probe.
;;; Actually exercising a live session is what examples/ncurses_demo.scm is
;;; for; run it by hand in a real terminal.

(import (curry ncurses))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;; Module import alone already dlopens libncurses and resolves every raw
;; binding's symbol via dlsym (define-foreign does this eagerly at
;; definition time) — reaching this line at all is itself a meaningful
;; assertion that the library loaded and every symbol this module uses
;; actually exists in it.
(check "module imported" #t #t)

;;; Attribute constants — NCURSES_BITS(mask, shift) with
;;; NCURSES_ATTR_SHIFT = 8, confirmed against a real system curses.h.

(check "a-normal"     a-normal     0)
(check "a-standout"   a-standout   65536)
(check "a-underline"  a-underline  131072)
(check "a-reverse"    a-reverse    262144)
(check "a-blink"      a-blink      524288)
(check "a-dim"        a-dim        1048576)
(check "a-bold"       a-bold       2097152)
(check "a-altcharset" a-altcharset 4194304)
(check "a-invis"      a-invis      8388608)
(check "a-protect"    a-protect    16777216)
(check "a-italic"     a-italic     2147483648)

;; Distinct, non-overlapping bits — combining any two via bitwise-or (as
;; with-attributes' docstring recommends) must not collide.
(check "bold and underline don't share bits"
  (bitwise-and a-bold a-underline) 0)
(check "bold | underline round-trips through bitwise-and"
  (bitwise-and (bitwise-or a-bold a-underline) a-bold) a-bold)

;;; ncurses-add-string!'s 3-arg form must validate argument types, not just
;;; arity — (curry ffi)'s c-string marshaling silently turns a non-string
;;; into a NULL pointer rather than raising, so a wrong-typed call must be
;;; rejected here, before it ever reaches the FFI boundary. These calls
;;; deliberately use a bogus win value ('fake-win, not a real window) to
;;; confirm the type check runs (and rejects) before ncurses-window-cptr is
;;; even called on it — no live session/terminal needed for this check.

(check "add-string!: 3-arg form rejects a non-string third argument"
  (guard (e (#t (error-object-message e))) (ncurses-add-string! 'fake-win 5 6 42))
  "ncurses-add-string!: expected (win str) or (win y x str)")

(check "add-string!: 3-arg form rejects non-integer y/x"
  (guard (e (#t (error-object-message e))) (ncurses-add-string! 'fake-win "a" "b" "c"))
  "ncurses-add-string!: expected (win str) or (win y x str)")

;;; ncurses-window? predicate shouldn't require a live session to reject
;;; non-windows.

(check "ncurses-window? rejects a plain value" (ncurses-window? 42) #f)
(check "ncurses-window? rejects a string" (ncurses-window? "not a window") #f)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(when (> fail 0)
  (error (string-append "ncurses_tests: " (number->string fail) " test(s) failed")))
