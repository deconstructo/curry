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

;;; ── Phase 3: issue #169 regression (unchecked bytevector data-argument
;;; casts in make-gl-texture/gl-texture-update!/gfx-draw-image!) ─────────────
;;;
;;; None of these checked their bytevector data argument at all before
;;; curry_bytevector_length/_ref's unchecked as_bytes() cast -- same class
;;; as #158/#161/#166/#167. gfx-draw-image! additionally computed its read
;;; loop bound from separate caller-supplied width/height arguments with no
;;; check against the bytevector's real backing length.

(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

(check "make-gl-texture rejects a non-bytevector argument (was a reproducible SIGSEGV)"
       (raises? (lambda () (make-gl-texture 42 8 8))) #t)
(check "make-gl-texture still works with a real bytevector"
       (raises? (lambda () (make-gl-texture (make-bytevector 64 0) 8 8))) #f)

(check "gl-texture-update! rejects a non-bytevector argument (was a reproducible SIGSEGV)"
       (raises? (lambda ()
                  (gl-texture-update! (make-gl-texture (make-bytevector 64 0) 8 8) 42)))
       #t)

(call-with-painter 100 100
  (lambda (painter)
    (check "gfx-draw-image! rejects a non-bytevector argument (was a reproducible SIGSEGV)"
           (raises? (lambda () (gfx-draw-image! painter 0 0 10 10 42 4 4))) #t)
    (check "gfx-draw-image! rejects a too-short bytevector (was a reproducible SIGSEGV / OOB read)"
           (raises? (lambda ()
                      (gfx-draw-image! painter 0 0 10 10 (make-bytevector 4 0) 4 4)))
           #t)
    (check "gfx-draw-image! rejects non-positive dimensions"
           (raises? (lambda ()
                      (gfx-draw-image! painter 0 0 10 10 (make-bytevector 64 0) -1 4)))
           #t)
    (check "gfx-draw-image! still works with a correctly-sized bytevector"
           (raises? (lambda ()
                      (gfx-draw-image! painter 0 0 10 10 (make-bytevector 64 128) 4 4)))
           #f)))

;;; ── Phase 4: issue #182 regression (more unchecked vector casts:
;;; gfx-draw-points!/gfx-draw-lines!/gfx-fill-triangles!/project-4d/
;;; rotate-4d-xw/make-gl-buffer) ──────────────────────────────────────────────
;;;
;;; None of these checked their vector argument(s) were actually vectors
;;; (or, for the fixed-length 4D helpers, long enough) before
;;; curry_vector_length/_ref's unchecked as_vec() cast -- same class as
;;; #169/#179. make-gl-buffer additionally never checked each vector
;;; ELEMENT was numeric before curry_float, which is itself unchecked for
;;; non-fixnum/flonum values.

(call-with-painter 400 200
  (lambda (painter)
    (check "gfx-draw-points! rejects a non-vector argument (was a reproducible SIGSEGV)"
           (raises? (lambda () (gfx-draw-points! painter 42 42 1.0 1.0 1.0 1.0 2.0))) #t)
    (check "gfx-draw-points! rejects a yvec shorter than xvec"
           (raises? (lambda ()
                      (gfx-draw-points! painter (vector 1.0 2.0 3.0) (vector 1.0)
                                        1.0 1.0 1.0 1.0 2.0)))
           #t)
    (check "gfx-draw-points! still works with matched real vectors"
           (raises? (lambda ()
                      (gfx-draw-points! painter (vector 10.0 20.0) (vector 30.0 40.0)
                                        1.0 1.0 1.0 1.0 2.0)))
           #f)
    (check "gfx-draw-lines! rejects a non-vector argument (was a reproducible SIGSEGV)"
           (raises? (lambda () (gfx-draw-lines! painter 42 1.0 1.0 1.0 1.0 2.0))) #t)
    (check "gfx-fill-triangles! rejects a non-vector argument (was a reproducible SIGSEGV)"
           (raises? (lambda () (gfx-fill-triangles! painter 42 1.0 1.0 1.0 1.0))) #t)))

(check "project-4d rejects a non-vector argument (was a reproducible SIGSEGV)"
       (raises? (lambda () (project-4d 42 42))) #t)
(check "project-4d rejects a too-short point vector"
       (raises? (lambda () (project-4d (vector 4.0 3.0) (vector 1.0 2.0)))) #t)
(check "project-4d still works with correctly-shaped vectors"
       (raises? (lambda () (project-4d (vector 4.0 3.0) (vector 1.0 2.0 3.0 0.5)))) #f)

(check "rotate-4d-xw rejects a non-vector argument (was a reproducible SIGSEGV)"
       (raises? (lambda () (rotate-4d-xw 42 1.0))) #t)
(check "rotate-4d-xw rejects a too-short point vector"
       (raises? (lambda () (rotate-4d-xw (vector 1.0 2.0) 0.5))) #t)
(check "rotate-4d-xw still works with a correctly-shaped vector"
       (raises? (lambda () (rotate-4d-xw (vector 1.0 2.0 3.0 4.0) 0.5))) #f)

(check "make-gl-buffer rejects a vector with a non-numeric element"
       (raises? (lambda () (make-gl-buffer (vector "x" "y" "z")))) #t)
(check "make-gl-buffer still works with a real vector of numbers"
       (raises? (lambda () (make-gl-buffer (vector 1.0 2.0 3.0)))) #f)

;;; ── Summary ──────────────────────────────────────────────────────────────────

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
