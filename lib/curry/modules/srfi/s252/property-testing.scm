(define-library (srfi s252 property-testing)
  (import (scheme base) (scheme write) (srfi s64 testing))
  (export
    ; Testing forms
    test-property test-property-expect-fail test-property-skip
    test-property-error test-property-error-type
    property-test-runner
    ; Basic-type generators
    boolean-generator char-generator string-generator symbol-generator
    bytevector-generator
    ; Number generators
    integer-generator real-generator rational-generator complex-generator
    number-generator
    exact-integer-generator exact-real-generator exact-rational-generator
    exact-complex-generator exact-integer-complex-generator
    exact-number-generator
    inexact-integer-generator inexact-real-generator inexact-rational-generator
    inexact-complex-generator inexact-number-generator
    ; Composite generators
    list-generator-of vector-generator-of pair-generator-of
    procedure-generator-of
    ; Internal helpers referenced directly from exported macro expansions.
    ; Not part of the SRFI-252 API — exported only because curry's
    ; syntax-rules macros resolve template identifiers in the use-site
    ; environment rather than the definition-site environment, so any
    ; procedure a public macro expands to must be visible there too (same
    ; reasoning as (srfi s64 testing)'s own %run-* exports).
    %test-property %test-property-expect-fail %test-property-error
    %test-property-skip)
  (begin

    ; SRFI-252 layers property-based testing on top of (srfi s64 testing):
    ; a "property" is a predicate applied to pseudorandom inputs across many
    ; runs, rather than a single hand-written assertion. Every generator
    ; here follows (srfi s158)'s thunk-of-values protocol but is infinite
    ; (never returns the eof-object) -- there's no dependency on s158 itself,
    ; any thunk with that shape works as a generator-list argument, including
    ; genuine s158 generators as long as they don't exhaust within `runs`
    ; calls (if one does, test-property raises rather than silently feeding
    ; the eof-object to the property, per the SRFI's own error condition).
    ;
    ; curry's random-integer/random-real (native builtins, no import needed)
    ; are used directly rather than going through (curry random)'s
    ; distribution objects -- these generators only need uniform sampling.
    ; There is no (current-random-source) parameter here: curry's RNG has a
    ; single global stream with no swappable-source abstraction (see (srfi
    ; s27 random-bits)'s default-random-source, which is the same global
    ; stream under a different name), so unlike the SRFI's own sample
    ; implementation, runs are not reproducible across processes by seeding
    ; a source object -- only by calling `random-source-pseudo-randomize!`
    ; on the global stream before the test runs.

    (define %default-property-runs 100)

    ; ── local helpers (kept independent of (srfi s1 lists), same reasoning
    ;    as (srfi s64 testing)'s own copies of these) ──────────────────────

    (define (%write->string v)
      (let ((out (open-output-string)))
        (write v out)
        (get-output-string out)))

    (define (%error->string e)
      (cond ((error-object? e)
             (string-append
               (error-object-message e)
               (if (null? (error-object-irritants e))
                   ""
                   (string-append " " (%write->string (error-object-irritants e))))))
            ((condition? e) (condition-message e))
            (else (%write->string e))))

    ; Same convention as (srfi s64 testing)'s test-error: a type is either
    ; #t (matches anything) or a one-argument predicate procedure. Curry has
    ; no SRFI-36 condition-type hierarchy, so a symbolic condition-type name
    ; (as in the SRFI's own &read-error example) isn't meaningful here --
    ; pass a predicate instead, e.g. `error-object?` or a custom guard.
    (define (%error-type-matches? type e)
      (cond ((eq? type #t) #t)
            ((procedure? type) (and (type e) #t))
            (else #t)))

    ; ── drawing inputs ──────────────────────────────────────────────────

    (define (%draw-args generator-list)
      (map (lambda (g)
             (let ((v (g)))
               (if (eof-object? v)
                   (error "srfi-252: generator exhausted before completing runs")
                   v)))
           generator-list))

    ; Runs `property` on `n` freshly drawn input tuples. Returns
    ; (raw-kind info args): raw-kind is 'pass if `property` was true on
    ; every run, 'fail if it returned #f on some run (args = that run's
    ; inputs), or 'error if it raised (info = the condition, args = that
    ; run's inputs).
    ;
    ; The recursive call to `loop` is deliberately kept OUTSIDE the `guard`
    ; form (evaluated only after `guard` has already returned a plain
    ; value) rather than in `guard`'s own body -- curry's `guard` is not
    ; transparent to tail calls the way ordinary `if`/`let` are (each
    ; `guard` invocation costs a real, unreclaimed stack frame until it
    ; returns), so recursing from inside it turned every trial run into an
    ; unbounded stack build-up: `(test-property ... 200000)` segfaulted.
    (define (%run-property-trials property generator-list n)
      (let loop ((i 0))
        (if (>= i n)
            (list 'pass n '())
            (let* ((args (%draw-args generator-list))
                   ; tag the outcome explicitly rather than branching on its
                   ; shape -- `property` legitimately returning a pair whose
                   ; car happens to be 'raised must not be confused with an
                   ; actual raised exception
                   (outcome (guard (e (#t (list 'raised e))) (list 'ok (apply property args)))))
              (cond
                ((eq? (car outcome) 'raised) (list 'error (cadr outcome) args))
                ((cadr outcome) (loop (+ i 1)))
                (else (list 'fail #f args)))))))

    ; Runs `property` on `n` freshly drawn input tuples, expecting it to
    ; raise a `type`-matching exception every time. Returns (raw-kind info
    ; args): 'pass if every run raised a matching exception, 'fail if some
    ; run returned normally (info = #f) or raised a non-matching exception
    ; (info = the condition). Same tail-position discipline as
    ; %run-property-trials above, for the same reason.
    (define (%run-error-trials property generator-list n type)
      (let loop ((i 0))
        (if (>= i n)
            (list 'pass n '())
            (let* ((args (%draw-args generator-list))
                   (outcome (guard (e (#t (list 'raised e))) (apply property args) 'no-raise)))
              (cond
                ((eq? outcome 'no-raise) (list 'fail #f args))
                ((%error-type-matches? type (cadr outcome)) (loop (+ i 1)))
                (else (list 'fail (cadr outcome) args)))))))

    ; ── recording one test-property-family case on the current runner ───
    ; (srfi s64 testing)'s own pass/fail/xpass/xfail/skip-count bookkeeping
    ; (the %do-case machinery behind test-assert etc.) is entirely private
    ; -- none of the count mutators (test-runner-pass-count! and friends)
    ; are exported, only their getters. So rather than reimplementing that
    ; bookkeeping here (and risking it drifting out of sync with the real
    ; thing), every property-test form is built on the two *exported*
    ; mechanisms s64 gives user code for exactly this purpose:
    ;   - %run-assert (name source-form thunk) -- the same primitive
    ;     test-assert itself expands to; a truthy thunk result is 'pass, a
    ;     falsy one is 'fail, a raised exception is caught and also 'fail.
    ;   - test-skip / test-expect-fail (specifier) -- register a name
    ;     pattern against the runner's skip-stack/fail-stack so the *next*
    ;     %do-case-driven test case matching that pattern is recorded as
    ;     'skip, or has its pass/fail outcome flipped to xpass/xfail.
    ; Giving each property-test call its own unique synthetic name (via a
    ; counter, since curry has no gensym) and registering a matching
    ; skip/expect-fail specifier just before running it gets skip and
    ; expect-fail semantics for free, verified correct by s64's own tests,
    ; rather than a second, hand-rolled copy of that logic here.
    ;
    ; The thunk passed to %run-assert only ever returns #t/#f -- both
    ; trial-runners above fully guard the property call themselves, so
    ; %run-assert's own exception handling is never exercised in practice.
    ; The richer failure detail (which inputs, which error) that %run-assert
    ; has no slot for needs to land in the SAME result-alist %do-case builds
    ; for this test case -- but %do-case clears and repopulates that alist
    ; immediately after the thunk returns and BEFORE firing on-test-end, so
    ; a test-result-set! called only after %run-assert itself returns is
    ; too late: on-test-end (where property-test-runner's own reporting
    ; hook reads these properties) has already fired by then, and any
    ; runner besides property-test-runner would see it as if it were never
    ; set at all. Attaching from inside %run-assert's own thunk is equally
    ; too early, for the same reason in reverse -- test-result-clear wipes
    ; it moments later. The one window that works is on-test-end itself:
    ; temporarily wrap the runner's on-test-end callback so this call's
    ; extra properties are attached first, then the original callback (the
    ; one that actually prints/records the result) runs as normal.

    (define %property-counter 0)

    (define (%next-property-name)
      (set! %property-counter (+ %property-counter 1))
      (string-append "srfi-252-property-" (number->string %property-counter)))

    (define (%run-property-form name source-form trials-thunk)
      (let ((runner (test-runner-get)) (args '()) (err #f))
        (let ((original-on-end (test-runner-on-test-end runner)))
          (test-runner-on-test-end! runner
            (lambda (r)
              (test-runner-on-test-end! r original-on-end)
              (test-result-set! r 'property-args args)
              (when err (test-result-set! r 'actual-error err))
              (original-on-end r))))
        (%run-assert name source-form
          (lambda ()
            (let ((raw (trials-thunk)))
              (set! args (caddr raw))
              ; raw's info slot (cadr raw) is the run count when raw-kind is
              ; 'pass -- a truthy non-#f number that must NOT be mistaken
              ; for an error object
              (when (and (not (eq? (car raw) 'pass)) (cadr raw)) (set! err (cadr raw)))
              (eq? (car raw) 'pass))))))

    (define (%test-property source-form property generator-list runs)
      (%run-property-form (%next-property-name) source-form
        (lambda () (%run-property-trials property generator-list (or runs %default-property-runs)))))

    (define (%test-property-expect-fail source-form property generator-list runs)
      (let ((name (%next-property-name)))
        (test-expect-fail name)
        (%run-property-form name source-form
          (lambda () (%run-property-trials property generator-list (or runs %default-property-runs))))))

    (define (%test-property-error source-form property generator-list runs type)
      (%run-property-form (%next-property-name) source-form
        (lambda () (%run-error-trials property generator-list (or runs %default-property-runs) type))))

    (define (%test-property-skip source-form)
      (let ((name (%next-property-name)))
        (test-skip name)
        ; never actually invoked -- %run-assert's %do-case checks the
        ; skip-stack before calling its thunk at all
        (%run-assert name source-form (lambda () #t))))

    ; ── testing forms ─────────────────────────────────────────────────────

    (define-syntax test-property
      (syntax-rules ()
        ((_ property glist) (%test-property 'property property glist #f))
        ((_ property glist runs) (%test-property 'property property glist runs))))

    (define-syntax test-property-expect-fail
      (syntax-rules ()
        ((_ property glist) (%test-property-expect-fail 'property property glist #f))
        ((_ property glist runs) (%test-property-expect-fail 'property property glist runs))))

    (define-syntax test-property-skip
      (syntax-rules ()
        ((_ property glist) (%test-property-skip 'property))
        ((_ property glist runs) (%test-property-skip 'property))))

    (define-syntax test-property-error
      (syntax-rules ()
        ((_ property glist) (%test-property-error 'property property glist #f #t))
        ((_ property glist runs) (%test-property-error 'property property glist runs #t))))

    (define-syntax test-property-error-type
      (syntax-rules ()
        ((_ error-type property glist)
         (%test-property-error 'property property glist #f error-type))
        ((_ error-type property glist runs)
         (%test-property-error 'property property glist runs error-type))))

    ; ── property-aware test runner ───────────────────────────────────────

    (define (test-on-test-end-property runner)
      (case (test-result-ref runner 'result-kind)
        ((fail)
         (display "FAIL (property): ") (write (test-result-ref runner 'source-form)) (newline)
         (let ((args (test-result-ref runner 'property-args #f)))
           (when (and args (pair? args))
             (display "  failing inputs: ") (write args) (newline)))
         (let ((err (test-result-ref runner 'actual-error #f)))
           (when err (display "  error: ") (display (%error->string err)) (newline))))
        ((xpass)
         (display "XPASS (property unexpectedly held for every input): ")
         (write (test-result-ref runner 'source-form)) (newline))
        ((xfail)
         (display "XFAIL (expected failure): ")
         (write (test-result-ref runner 'source-form)) (newline))
        (else (if #f #f))))

    (define (property-test-runner)
      (let ((r (test-runner-simple)))
        (test-runner-on-test-end! r test-on-test-end-property)
        r))

    ; ── generators ──────────────────────────────────────────────────────
    ; Each returns a fresh, independent generator (a thunk). The first call
    ; or two produce the fixed "interesting values" the SRFI specifies;
    ; every call after that draws uniformly at random.

    (define (%with-prefix prefix tail-thunk)
      (let ((remaining prefix))
        (lambda ()
          (if (pair? remaining)
              (let ((v (car remaining))) (set! remaining (cdr remaining)) v)
              (tail-thunk)))))

    (define %char-codepoint-limit #x110000)

    (define (%random-char)
      (let loop ()
        (let ((cp (random-integer %char-codepoint-limit)))
          (if (and (>= cp #xD800) (<= cp #xDFFF)) (loop) (integer->char cp)))))

    (define %string-max-length 16)

    (define (%random-string)
      (let* ((len (random-integer (+ %string-max-length 1)))
             (s (make-string len)))
        (let loop ((i 0))
          (if (< i len)
              (begin (string-set! s i (%random-char)) (loop (+ i 1)))
              s))))

    (define %bytevector-max-length 16)

    (define (%random-bytevector)
      (let* ((len (random-integer (+ %bytevector-max-length 1)))
             (bv (make-bytevector len)))
        (let loop ((i 0))
          (if (< i len)
              (begin (bytevector-u8-set! bv i (random-integer 256)) (loop (+ i 1)))
              bv))))

    (define (boolean-generator)
      (%with-prefix (list #t #f) (lambda () (< (random-real) 0.5))))

    (define (char-generator)
      (%with-prefix (list #\null) %random-char))

    (define (string-generator)
      (%with-prefix (list "") %random-string))

    (define (symbol-generator)
      (%with-prefix (list (string->symbol "")) (lambda () (string->symbol (%random-string)))))

    (define (bytevector-generator)
      (%with-prefix (list (bytevector)) %random-bytevector))

    ; numbers

    (define %int-magnitude 1000000)

    (define (%random-exact-integer)
      (- (random-integer (+ (* 2 %int-magnitude) 1)) %int-magnitude))

    (define %rational-num-magnitude 1000)
    (define %rational-den-magnitude 1000)

    (define (%random-exact-rational)
      (/ (- (random-integer (+ (* 2 %rational-num-magnitude) 1)) %rational-num-magnitude)
         (+ 1 (random-integer %rational-den-magnitude))))

    (define %flonum-magnitude 1000000.0)

    (define (%random-flonum)
      (* (- (random-real) 0.5) (* 2.0 %flonum-magnitude)))

    (define (exact-integer-generator)
      (%with-prefix (list 0 1 -1) %random-exact-integer))

    (define (exact-rational-generator)
      (%with-prefix (list 0 1 -1 1/2 -1/2) %random-exact-rational))

    (define (exact-real-generator)
      (%with-prefix (list 0 1 -1 1/2 -1/2) %random-exact-rational))

    ; curry's complex numbers are always inexact internally (double re/im),
    ; even when constructed from exact parts -- (exact? (make-rectangular
    ; 1/2 1/3)) is #f -- so exact complex numbers don't exist to generate.
    ; The SRFI explicitly allows raising an error here in that case.
    (define (exact-complex-generator)
      (error "exact-complex-generator: curry has no exact complex numbers"))

    (define (exact-integer-complex-generator)
      (error "exact-integer-complex-generator: curry has no exact complex numbers"))

    (define (exact-number-generator)
      (let ((int-gen (exact-integer-generator)) (rat-gen (exact-rational-generator)))
        (lambda () (if (< (random-real) 0.5) (int-gen) (rat-gen)))))

    (define (inexact-integer-generator)
      (%with-prefix (list 0.0 -0.0 1.0 -1.0)
        (lambda () (exact->inexact (%random-exact-integer)))))

    (define (inexact-rational-generator)
      (%with-prefix (list 0.0 -0.0 0.5 -0.5 1.0 -1.0)
        (lambda () (exact->inexact (%random-exact-rational)))))

    (define (inexact-real-generator)
      (%with-prefix (list 0.0 -0.0 0.5 -0.5 1.0 -1.0 +inf.0 -inf.0 +nan.0)
        %random-flonum))

    (define (inexact-complex-generator)
      (%with-prefix
        (list (make-rectangular 0.0 0.0)
              (make-rectangular +inf.0 0.0) (make-rectangular -inf.0 0.0)
              (make-rectangular 0.0 +inf.0) (make-rectangular 0.0 -inf.0)
              (make-rectangular +nan.0 0.0) (make-rectangular 0.0 +nan.0))
        (lambda () (make-rectangular (%random-flonum) (%random-flonum)))))

    (define (inexact-number-generator)
      (let ((int-gen (inexact-integer-generator)) (cx-gen (inexact-complex-generator)))
        (lambda () (if (< (random-real) 0.5) (int-gen) (cx-gen)))))

    (define (integer-generator)
      (let ((e (exact-integer-generator)) (i (inexact-integer-generator)))
        (lambda () (if (< (random-real) 0.5) (e) (i)))))

    (define (real-generator)
      (let ((e (exact-real-generator)) (i (inexact-real-generator)))
        (lambda () (if (< (random-real) 0.5) (e) (i)))))

    (define (rational-generator)
      (let ((e (exact-rational-generator)) (i (inexact-rational-generator)))
        (lambda () (if (< (random-real) 0.5) (e) (i)))))

    ; No exact-complex support (see exact-complex-generator above), so this
    ; aliases inexact-complex-generator outright -- exactly what the SRFI
    ; says to do in that situation.
    (define (complex-generator) (inexact-complex-generator))

    (define (number-generator)
      (let ((e (exact-number-generator)) (i (inexact-number-generator)))
        (lambda () (if (< (random-real) 0.5) (e) (i)))))

    ; ── composite generators ──────────────────────────────────────────────

    (define %composite-max-length 8)

    (define (list-generator-of subgen . opt)
      (let ((maxlen (if (pair? opt) (car opt) %composite-max-length)))
        (%with-prefix (list '())
          (lambda ()
            (let ((len (+ 1 (random-integer maxlen))))
              (let loop ((i 0) (acc '()))
                (if (>= i len) acc (loop (+ i 1) (cons (subgen) acc)))))))))

    (define (vector-generator-of subgen . opt)
      (let ((maxlen (if (pair? opt) (car opt) %composite-max-length)))
        (%with-prefix (list (vector))
          (lambda ()
            (let* ((len (+ 1 (random-integer maxlen)))
                   (v (make-vector len)))
              (let loop ((i 0))
                (if (< i len)
                    (begin (vector-set! v i (subgen)) (loop (+ i 1)))
                    v)))))))

    (define (pair-generator-of car-gen . opt)
      (let ((cdr-gen (if (pair? opt) (car opt) car-gen)))
        (lambda () (cons (car-gen) (cdr-gen)))))

    ; Each call produces a fresh procedure, ignoring whatever arguments it's
    ; later called with and returning one value sampled from `subgen` at
    ; the moment the procedure was generated (not at call time) -- matching
    ; the SRFI's own example, where each call site re-invokes the outer
    ; generator to get a new procedure rather than reusing one.
    (define (procedure-generator-of subgen)
      (lambda () (let ((v (subgen))) (lambda args v))))))
