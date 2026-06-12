;;; test_qt6_new.scm — automated tests for make-scroll-area and gfx text metrics
;;;
;;; Both phases run without an event loop:
;;;   Phase 1 — widget construction: QApplication exists from import, no window needed.
;;;   Phase 2 — text metrics: call-with-painter creates a QPixmap-backed painter
;;;             without a GL context or display, so the test is fully headless.
;;;
;;; Run:  CURRY_MODULE_PATH=build/mods ./build/curry tests/test_qt6_new.scm
;;; CI:   QT_QPA_PLATFORM=offscreen  (same command; no display required)

(import (curry qt6))
(import (scheme base))

;;; ── Harness ──────────────────────────────────────────────────────────────────

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write result)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (check-pred label pred result)
  (if (pred result)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — unexpected value: ") (write result) (newline)
             (set! fail (+ fail 1)))))

;;; ── Phase 1: make-scroll-area ────────────────────────────────────────────────

(display "--- Phase 1: make-scroll-area ---\n")

;;; Construction
(define sa (make-scroll-area))
(check-pred "make-scroll-area returns a value"
            (lambda (x) (not (eq? x #f))) sa)

;;; layout-add! treats a scroll-area the same as make-vbox
(define lbl-a (make-label "Alpha"))
(define lbl-b (make-label "Beta"))
(define lbl-c (make-label "Gamma"))
(define sl    (make-slider "s" 0 100 1 50 (lambda (v) v)))
(define btn   (make-button "Click" (lambda () #f)))
(define sep   (make-separator))

(check "layout-add! label (1)"      (begin (layout-add! sa lbl-a) #t) #t)
(check "layout-add! label (2)"      (begin (layout-add! sa lbl-b) #t) #t)
(check "layout-add! slider"         (begin (layout-add! sa sl)    #t) #t)
(check "layout-add! button"         (begin (layout-add! sa btn)   #t) #t)
(check "layout-add! separator"      (begin (layout-add! sa sep)   #t) #t)
(check "layout-add! label (3)"      (begin (layout-add! sa lbl-c) #t) #t)

;;; Widget property setters apply to the QScrollArea widget itself
(check "widget-set-min-size!"    (begin (widget-set-min-size! sa 100  50)    #t) #t)
(check "widget-set-max-size!"    (begin (widget-set-max-size! sa 800 2000)   #t) #t)
(check "widget-set-tooltip!"     (begin (widget-set-tooltip! sa "tip")       #t) #t)
(check "widget-set-enabled!"     (begin (widget-set-enabled! sa #t)          #t) #t)
(check "widget-set-visible!"     (begin (widget-set-visible! sa #t)          #t) #t)

;;; Nesting: scroll-area inside a vbox, and another scroll-area inside that
(define outer (make-vbox))
(define inner-sa (make-scroll-area))
(layout-add! inner-sa (make-label "nested 1"))
(layout-add! inner-sa (make-label "nested 2"))
(layout-add! inner-sa (make-slider "n" 0 10 1 5 (lambda (v) v)))
(check "scroll-area nested in vbox"  (begin (layout-add! outer inner-sa) #t) #t)

;;; scroll-area can also contain a group-box (itself a vbox-backed widget)
(define sa2 (make-scroll-area))
(define gb  (make-group-box "Group"))
(layout-add! gb  (make-label "inside group"))
(check "scroll-area contains group-box"  (begin (layout-add! sa2 gb) #t) #t)

;;; ── Phase 2: gfx text metrics via call-with-painter ─────────────────────────
;;;
;;; call-with-painter creates a QPixmap-backed QPainter without any GL context
;;; or window — fully headless, no event loop required.

(display "--- Phase 2: gfx text metrics ---\n")

;;; Capture all metrics in a single painter session at 12pt
(define w-hello   #f)
(define w-longer  #f)
(define w-empty   #f)
(define w-same-1  #f)
(define w-same-2  #f)
(define asc-12    #f)
(define desc-12   #f)
(define ht-12     #f)
(define w-m-12    #f)

(call-with-painter 400 200
  (lambda (painter)
    (gfx-set-font! painter "Helvetica" 12)
    (set! w-hello  (gfx-text-width painter "Hello"))
    (set! w-longer (gfx-text-width painter
                     "Hello, World! This is a noticeably longer string."))
    (set! w-empty  (gfx-text-width painter ""))
    (set! w-same-1 (gfx-text-width painter "ABC"))
    (set! w-same-2 (gfx-text-width painter "ABC"))
    (set! asc-12   (gfx-font-ascent  painter))
    (set! desc-12  (gfx-font-descent painter))
    (set! ht-12    (gfx-font-height  painter))
    (set! w-m-12   (gfx-text-width painter "M"))))

;;; Capture 24pt metrics in a second session
(define asc-24  #f)
(define ht-24   #f)
(define w-m-24  #f)

(call-with-painter 400 200
  (lambda (painter)
    (gfx-set-font! painter "Helvetica" 24)
    (set! asc-24  (gfx-font-ascent  painter))
    (set! ht-24   (gfx-font-height  painter))
    (set! w-m-24  (gfx-text-width painter "M"))))

;;; gfx-text-width — type and sign
(check-pred "text-width 'Hello' is a positive number"
            (lambda (v) (and (number? v) (> v 0)))
            w-hello)

(check-pred "text-width empty string is zero"
            (lambda (v) (and (number? v) (= v 0)))
            w-empty)

;;; Monotone: longer string strictly wider
(check-pred "longer string wider than 'Hello'"
            (lambda (v) (and (number? v) (> v w-hello)))
            w-longer)

;;; Deterministic: same string, same font, same value
(check "text-width is deterministic (same string measured twice)"
       w-same-1 w-same-2)

;;; gfx-font-ascent / descent / height — types and signs
(check-pred "font-ascent 12pt is positive"
            (lambda (v) (and (number? v) (> v 0)))
            asc-12)

(check-pred "font-descent 12pt is positive"
            (lambda (v) (and (number? v) (> v 0)))
            desc-12)

(check-pred "font-height 12pt is positive"
            (lambda (v) (and (number? v) (> v 0)))
            ht-12)

;;; font-height >= ascent + descent (Qt may add leading)
(check-pred "font-height >= ascent + descent"
            (lambda (v) (>= v (+ asc-12 desc-12)))
            ht-12)

;;; font-height > ascent alone
(check-pred "font-height > ascent"
            (lambda (v) (> v asc-12))
            ht-12)

;;; Scaling: 24pt must be strictly larger than 12pt for every metric
(check-pred "font-ascent grows from 12pt to 24pt"
            (lambda (v) (> v asc-12))
            asc-24)

(check-pred "font-height grows from 12pt to 24pt"
            (lambda (v) (> v ht-12))
            ht-24)

(check-pred "text-width 'M' grows from 12pt to 24pt"
            (lambda (v) (> v w-m-12))
            w-m-24)

;;; Rough proportionality: 24pt 'M' should be 1.4–3× the 12pt 'M'
;;; (exact ratio varies by platform and font hinting)
(check-pred "'M' width roughly doubles from 12pt to 24pt (1.4×–3×)"
            (lambda (v) (and (> v (* 1.4 w-m-12))
                             (< v (* 3.0 w-m-12))))
            w-m-24)

;;; gfx-set-font! change is visible in metrics within one painter session
(define ht-after-font-change #f)
(call-with-painter 100 50
  (lambda (painter)
    (gfx-set-font! painter "Helvetica" 12)
    (let ((h1 (gfx-font-height painter)))
      (gfx-set-font! painter "Helvetica" 20)
      (set! ht-after-font-change (gfx-font-height painter)))))

(check-pred "font-height increases after gfx-set-font! to larger size"
            (lambda (v) (> v ht-12))
            ht-after-font-change)

;;; ── Summary ──────────────────────────────────────────────────────────────────

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
