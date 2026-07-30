(define-library (srfi s64 testing)
  (import (scheme base) (scheme write))
  (export
    ; Test cases
    test-assert test-eqv test-eq test-equal test-approximate test-error
    test-read-eval-string
    ; Test groups
    test-begin test-end test-group test-group-with-cleanup
    ; Test control / specifiers
    test-skip test-expect-fail
    test-match-name test-match-nth test-match-any test-match-all
    ; Test runners
    test-runner? test-runner-current test-runner-get
    test-runner-create test-runner-null test-runner-simple
    test-runner-factory test-apply test-with-runner
    ; Results
    test-result-kind test-passed?
    test-result-ref test-result-set! test-result-remove
    test-result-clear test-result-alist
    ; Runner accessors
    test-runner-pass-count test-runner-fail-count
    test-runner-xpass-count test-runner-xfail-count
    test-runner-skip-count
    test-runner-test-name
    test-runner-group-path test-runner-group-stack
    test-runner-aux-value test-runner-aux-value!
    test-runner-reset
    test-runner-on-test-begin test-runner-on-test-begin!
    test-runner-on-test-end test-runner-on-test-end!
    test-runner-on-group-begin test-runner-on-group-begin!
    test-runner-on-group-end test-runner-on-group-end!
    test-runner-on-bad-count test-runner-on-bad-count!
    test-runner-on-bad-end-name test-runner-on-bad-end-name!
    test-runner-on-final test-runner-on-final!
    ; Default callback implementations
    test-on-test-begin-simple test-on-test-end-simple
    test-on-group-begin-simple test-on-group-end-simple
    test-on-bad-count-simple test-on-bad-end-name-simple
    test-on-final-simple
    ; Internal helpers referenced directly from exported macro expansions.
    ; Not part of the SRFI-64 API — exported only because curry's
    ; syntax-rules macros resolve template identifiers in the use-site
    ; environment rather than the definition-site environment, so any
    ; procedure a public macro expands to must be visible there too.
    %run-assert %run-compare %run-approx %run-error %run-error-2 %run-group)
  (begin

    ; ── small local helpers (kept independent of (srfi s1 lists)) ──────

    (define (%any pred lst)
      (cond ((null? lst) #f)
            ((pred (car lst)) #t)
            (else (%any pred (cdr lst)))))

    (define (%write->string v)
      (let ((out (open-output-string)))
        (write v out)
        (get-output-string out)))

    (define (%join-strings strs sep)
      (cond ((null? strs) "")
            ((null? (cdr strs)) (car strs))
            (else (string-append (car strs) sep (%join-strings (cdr strs) sep)))))

    (define (%error->string e)
      (cond ((error-object? e)
             (string-append
               (error-object-message e)
               (if (null? (error-object-irritants e))
                   ""
                   (string-append " " (%write->string (error-object-irritants e))))))
            ((condition? e) (condition-message e))
            (else (%write->string e))))

    ; ── the test-runner record ─────────────────────────────────────────

    (define-record-type <test-runner>
      (%make-test-runner)
      test-runner?
      (pass-count        test-runner-pass-count        test-runner-pass-count!)
      (fail-count        test-runner-fail-count        test-runner-fail-count!)
      (xpass-count       test-runner-xpass-count       test-runner-xpass-count!)
      (xfail-count       test-runner-xfail-count       test-runner-xfail-count!)
      (skip-count        test-runner-skip-count        test-runner-skip-count!)
      (test-name         test-runner-test-name         test-runner-test-name!)
      (group-stack       test-runner-group-stack       test-runner-group-stack!)
      (skip-stack        %test-runner-skip-stack       %test-runner-skip-stack!)
      (fail-stack        %test-runner-fail-stack       %test-runner-fail-stack!)
      (count-stack       %test-runner-count-stack      %test-runner-count-stack!)
      (actual-stack      %test-runner-actual-stack     %test-runner-actual-stack!)
      (result-alist      test-runner-result-alist      test-runner-result-alist!)
      (aux-value         test-runner-aux-value         test-runner-aux-value!)
      (self-installed    %test-runner-self-installed   %test-runner-self-installed!)
      (on-test-begin     test-runner-on-test-begin     test-runner-on-test-begin!)
      (on-test-end       test-runner-on-test-end       test-runner-on-test-end!)
      (on-group-begin    test-runner-on-group-begin    test-runner-on-group-begin!)
      (on-group-end      test-runner-on-group-end      test-runner-on-group-end!)
      (on-bad-count      test-runner-on-bad-count      test-runner-on-bad-count!)
      (on-bad-end-name   test-runner-on-bad-end-name   test-runner-on-bad-end-name!)
      (on-final          test-runner-on-final          test-runner-on-final!))

    (define (%noop . args) (if #f #f))

    (define (test-runner-null)
      (let ((r (%make-test-runner)))
        (test-runner-pass-count!  r 0)
        (test-runner-fail-count!  r 0)
        (test-runner-xpass-count! r 0)
        (test-runner-xfail-count! r 0)
        (test-runner-skip-count!  r 0)
        (test-runner-test-name!   r "")
        (test-runner-group-stack! r '())
        (%test-runner-skip-stack!   r '())
        (%test-runner-fail-stack!   r '())
        (%test-runner-count-stack!  r '())
        (%test-runner-actual-stack! r '())
        (test-runner-result-alist! r '())
        (test-runner-aux-value!    r #f)
        (%test-runner-self-installed! r #f)
        (test-runner-on-test-begin!    r %noop)
        (test-runner-on-test-end!      r %noop)
        (test-runner-on-group-begin!   r %noop)
        (test-runner-on-group-end!     r %noop)
        (test-runner-on-bad-count!     r %noop)
        (test-runner-on-bad-end-name!  r %noop)
        (test-runner-on-final!         r %noop)
        r))

    (define (test-runner-reset runner)
      (test-runner-pass-count!  runner 0)
      (test-runner-fail-count!  runner 0)
      (test-runner-xpass-count! runner 0)
      (test-runner-xfail-count! runner 0)
      (test-runner-skip-count!  runner 0)
      (test-runner-test-name!   runner "")
      (test-runner-group-stack! runner '())
      (%test-runner-skip-stack!   runner '())
      (%test-runner-fail-stack!   runner '())
      (%test-runner-count-stack!  runner '())
      (%test-runner-actual-stack! runner '())
      (test-runner-result-alist! runner '()))

    (define (test-runner-group-path runner)
      (reverse (test-runner-group-stack runner)))

    ; ── default ("simple") callbacks ───────────────────────────────────

    (define (%full-name runner)
      (let ((path (test-runner-group-path runner))
            (name (test-runner-test-name runner)))
        (%join-strings (append path (if (equal? name "") '() (list name))) " > ")))

    (define (test-on-test-begin-simple runner) (if #f #f))

    (define (test-on-test-end-simple runner)
      (case (test-result-ref runner 'result-kind)
        ((fail)
         (display "FAIL: ") (display (%full-name runner)) (newline)
         (let ((sf (test-result-ref runner 'source-form #f)))
           (when sf (display "  form: ") (write sf) (newline)))
         (let ((err (test-result-ref runner 'actual-error #f)))
           (if err
               (begin (display "  error: ") (display (%error->string err)) (newline))
               (let ((val (test-result-ref runner 'actual-value #f)))
                 (display "  got: ") (write val) (newline)))))
        ((xpass)
         (display "XPASS (unexpected pass): ") (display (%full-name runner)) (newline))
        ((xfail)
         (display "XFAIL (expected failure): ") (display (%full-name runner)) (newline))
        (else (if #f #f))))

    (define (test-on-group-begin-simple runner suite-name count) (if #f #f))
    (define (test-on-group-end-simple runner) (if #f #f))

    (define (test-on-bad-count-simple runner actual-count expected-count)
      (display "*** Total number of tests should be ") (display expected-count)
      (display " but was ") (display actual-count) (display " ***") (newline))

    (define (test-on-bad-end-name-simple runner begin-name end-name)
      (display "*** test-end name ") (write end-name)
      (display " does not match test-begin name ") (write begin-name)
      (display " ***") (newline))

    (define (test-on-final-simple runner)
      (let ((pass  (test-runner-pass-count runner))
            (fail  (test-runner-fail-count runner))
            (xpass (test-runner-xpass-count runner))
            (xfail (test-runner-xfail-count runner))
            (skip  (test-runner-skip-count runner)))
        (newline)
        (display pass) (display " passed, ") (display fail) (display " failed")
        (when (> xpass 0) (display ", ") (display xpass) (display " unexpectedly passed"))
        (when (> xfail 0) (display ", ") (display xfail) (display " expected failures"))
        (when (> skip 0)  (display ", ") (display skip)  (display " skipped"))
        (newline)
        (exit (if (or (> fail 0) (> xpass 0)) 1 0))))

    (define (test-runner-simple)
      (let ((r (test-runner-null)))
        (test-runner-on-test-end!     r test-on-test-end-simple)
        (test-runner-on-bad-count!    r test-on-bad-count-simple)
        (test-runner-on-bad-end-name! r test-on-bad-end-name-simple)
        (test-runner-on-final!        r test-on-final-simple)
        r))

    ; ── current runner / factory ───────────────────────────────────────

    (define test-runner-current (make-parameter #f))
    (define test-runner-factory (make-parameter test-runner-simple))

    (define (test-runner-get)
      (or (test-runner-current)
          (error "test-runner-get: no current test runner")))

    (define (test-runner-create) ((test-runner-factory)))

    (define-syntax test-with-runner
      (syntax-rules ()
        ((_ runner decl ...)
         (parameterize ((test-runner-current runner)) decl ...))))

    ; ── result properties ──────────────────────────────────────────────

    (define (test-result-ref runner pname . opt)
      (let ((pair (assq pname (test-runner-result-alist runner))))
        (if pair (cdr pair) (if (null? opt) #f (car opt)))))

    (define (test-result-set! runner pname value)
      (test-runner-result-alist!
        runner
        (cons (cons pname value)
              (filter (lambda (p) (not (eq? (car p) pname)))
                      (test-runner-result-alist runner)))))

    (define (test-result-remove runner pname)
      (test-runner-result-alist!
        runner
        (filter (lambda (p) (not (eq? (car p) pname)))
                (test-runner-result-alist runner))))

    (define (test-result-clear runner)
      (test-runner-result-alist! runner '()))

    (define (test-result-alist runner)
      (test-runner-result-alist runner))

    (define (test-result-kind . opt)
      (let ((runner (if (null? opt) (test-runner-current) (car opt))))
        (and runner (test-result-ref runner 'result-kind))))

    (define (test-passed? . opt)
      (and (memv (apply test-result-kind opt) '(pass xpass)) #t))

    ; ── match specifiers ───────────────────────────────────────────────
    ; A specifier is a one-argument procedure: (spec test-name) -> boolean.

    (define (test-match-name name)
      (lambda (tname) (equal? name tname)))

    (define (test-match-nth n . opt)
      (let ((count (if (null? opt) 1 (car opt)))
            (i 0))
        (lambda (tname)
          (set! i (+ i 1))
          (and (>= i n) (< i (+ n count))))))

    (define (test-match-any . specs)
      (lambda (tname)
        (let loop ((s specs) (matched #f))
          (if (null? s)
              matched
              (loop (cdr s) (or ((car s) tname) matched))))))

    (define (test-match-all . specs)
      (lambda (tname)
        (let loop ((s specs) (ok #t))
          (if (null? s)
              ok
              (loop (cdr s) (and ((car s) tname) ok))))))

    (define (%as-specifier x)
      (if (string? x) (test-match-name x) x))

    (define (test-skip specifier)
      (let* ((runner (test-runner-get))
             (spec (%as-specifier specifier))
             (stack (%test-runner-skip-stack runner)))
        (%test-runner-skip-stack!
          runner
          (if (null? stack)
              (list (list spec))
              (cons (cons spec (car stack)) (cdr stack))))))

    (define (test-expect-fail specifier)
      (let* ((runner (test-runner-get))
             (spec (%as-specifier specifier))
             (stack (%test-runner-fail-stack runner)))
        (%test-runner-fail-stack!
          runner
          (if (null? stack)
              (list (list spec))
              (cons (cons spec (car stack)) (cdr stack))))))

    (define (%specifiers-match? stack name)
      (%any (lambda (spec) (spec name)) (apply append stack)))

    ; ── core test-case machinery ───────────────────────────────────────

    (define (%bump-actual-count! runner)
      (let ((stack (%test-runner-actual-stack runner)))
        (unless (null? stack)
          (%test-runner-actual-stack! runner (cons (+ 1 (car stack)) (cdr stack))))))

    (define (%do-case name source-form result-thunk)
      (let* ((runner (test-runner-get))
             (nm (or name "")))
        (test-runner-test-name! runner nm)
        (%bump-actual-count! runner)
        (if (%specifiers-match? (%test-runner-skip-stack runner) nm)
            (begin
              (test-runner-skip-count! runner (+ 1 (test-runner-skip-count runner)))
              (test-result-clear runner)
              (test-result-set! runner 'result-kind 'skip)
              (test-result-set! runner 'test-name nm)
              (test-result-set! runner 'source-form source-form)
              ((test-runner-on-test-begin runner) runner)
              ((test-runner-on-test-end runner) runner))
            (let ((expect-fail? (%specifiers-match? (%test-runner-fail-stack runner) nm)))
              ((test-runner-on-test-begin runner) runner)
              (let* ((raw (result-thunk))
                     (raw-kind (car raw))
                     (value (cadr raw))
                     (kind (if (eq? raw-kind 'pass)
                               (if expect-fail? 'xpass 'pass)
                               (if expect-fail? 'xfail 'fail))))
                (test-result-clear runner)
                (test-result-set! runner 'result-kind kind)
                (test-result-set! runner 'test-name nm)
                (test-result-set! runner 'source-form source-form)
                (if (eq? raw-kind 'error)
                    (test-result-set! runner 'actual-error value)
                    (test-result-set! runner 'actual-value value))
                (case kind
                  ((pass)  (test-runner-pass-count!  runner (+ 1 (test-runner-pass-count runner))))
                  ((fail)  (test-runner-fail-count!  runner (+ 1 (test-runner-fail-count runner))))
                  ((xpass) (test-runner-xpass-count! runner (+ 1 (test-runner-xpass-count runner))))
                  ((xfail) (test-runner-xfail-count! runner (+ 1 (test-runner-xfail-count runner)))))
                ((test-runner-on-test-end runner) runner))))))

    ; ── test cases ──────────────────────────────────────────────────────

    (define (%run-assert name source-form thunk)
      (%do-case name source-form
        (lambda ()
          (guard (e (#t (list 'error e)))
            (let ((v (thunk))) (list (if v 'pass 'fail) v))))))

    (define-syntax test-assert
      (syntax-rules ()
        ((_ expr) (%run-assert #f 'expr (lambda () expr)))
        ((_ name expr) (%run-assert name 'expr (lambda () expr)))))

    (define (%run-compare = expected thunk name source-form)
      (%do-case name source-form
        (lambda ()
          (guard (e (#t (list 'error e)))
            (let ((v (thunk))) (list (if (= expected v) 'pass 'fail) v))))))

    (define-syntax test-equal
      (syntax-rules ()
        ((_ expected expr) (%run-compare equal? expected (lambda () expr) #f 'expr))
        ((_ name expected expr) (%run-compare equal? expected (lambda () expr) name 'expr))))

    (define-syntax test-eqv
      (syntax-rules ()
        ((_ expected expr) (%run-compare eqv? expected (lambda () expr) #f 'expr))
        ((_ name expected expr) (%run-compare eqv? expected (lambda () expr) name 'expr))))

    (define-syntax test-eq
      (syntax-rules ()
        ((_ expected expr) (%run-compare eq? expected (lambda () expr) #f 'expr))
        ((_ name expected expr) (%run-compare eq? expected (lambda () expr) name 'expr))))

    (define (%run-approx name source-form expected thunk err)
      (%do-case name source-form
        (lambda ()
          (guard (e (#t (list 'error e)))
            (let ((v (thunk)))
              (list (if (<= (abs (- v expected)) err) 'pass 'fail) v))))))

    (define-syntax test-approximate
      (syntax-rules ()
        ((_ expected expr err) (%run-approx #f 'expr expected (lambda () expr) err))
        ((_ name expected expr err) (%run-approx name 'expr expected (lambda () expr) err))))

    (define (%error-type-matches? type e)
      (cond ((eq? type #t) #t)
            ((procedure? type) (and (type e) #t))
            (else #t)))

    (define (%run-error name type source-form thunk)
      (%do-case name source-form
        (lambda ()
          (guard (e (#t (list (if (%error-type-matches? type e) 'pass 'fail) e)))
            (thunk)
            (list 'fail #f)))))

    (define (%run-error-2 name-or-type source-form thunk)
      (if (string? name-or-type)
          (%run-error name-or-type #t source-form thunk)
          (%run-error #f name-or-type source-form thunk)))

    (define-syntax test-error
      (syntax-rules ()
        ((_ expr) (%run-error #f #t 'expr (lambda () expr)))
        ((_ name-or-type expr) (%run-error-2 name-or-type 'expr (lambda () expr)))
        ((_ name type expr) (%run-error name type 'expr (lambda () expr)))))

    (define (test-read-eval-string str)
      (let* ((port (open-input-string str))
             (form (read port))
             (extra (read port)))
        (if (eof-object? extra)
            (eval form (interaction-environment))
            (error "test-read-eval-string: extra data after form" str))))

    ; ── test groups ─────────────────────────────────────────────────────

    (define (%group-should-skip? name)
      (let ((runner (test-runner-current)))
        (and runner (%specifiers-match? (%test-runner-skip-stack runner) name))))

    (define (test-begin suite-name . opt)
      (let ((count (if (null? opt) #f (car opt))))
        (unless (test-runner-current)
          (let ((r (test-runner-create)))
            (%test-runner-self-installed! r #t)
            (test-runner-current r)))
        (let ((runner (test-runner-current)))
          (test-runner-group-stack! runner (cons suite-name (test-runner-group-stack runner)))
          (%test-runner-skip-stack!   runner (cons '() (%test-runner-skip-stack runner)))
          (%test-runner-fail-stack!   runner (cons '() (%test-runner-fail-stack runner)))
          (%test-runner-count-stack!  runner (cons count (%test-runner-count-stack runner)))
          (%test-runner-actual-stack! runner (cons 0 (%test-runner-actual-stack runner)))
          ((test-runner-on-group-begin runner) runner suite-name count))))

    (define (test-end . opt)
      (let* ((runner (test-runner-get))
             (given-name (if (null? opt) #f (car opt)))
             (stack (test-runner-group-stack runner)))
        (when (null? stack)
          (error "test-end: no matching test-begin"))
        (let ((begin-name (car stack))
              (expected-count (car (%test-runner-count-stack runner)))
              (actual-count (car (%test-runner-actual-stack runner))))
          (when (and given-name (not (equal? given-name begin-name)))
            ((test-runner-on-bad-end-name runner) runner begin-name given-name))
          (when (and expected-count (not (= expected-count actual-count)))
            ((test-runner-on-bad-count runner) runner actual-count expected-count))
          (test-runner-group-stack! runner (cdr stack))
          (%test-runner-skip-stack!   runner (cdr (%test-runner-skip-stack runner)))
          (%test-runner-fail-stack!   runner (cdr (%test-runner-fail-stack runner)))
          (%test-runner-count-stack!  runner (cdr (%test-runner-count-stack runner)))
          (%test-runner-actual-stack! runner (cdr (%test-runner-actual-stack runner)))
          (let ((rest (%test-runner-actual-stack runner)))
            (unless (null? rest)
              (%test-runner-actual-stack! runner (cons (+ actual-count (car rest)) (cdr rest)))))
          ((test-runner-on-group-end runner) runner)
          (when (and (null? (test-runner-group-stack runner))
                     (%test-runner-self-installed runner))
            ((test-runner-on-final runner) runner)
            (test-runner-current #f)))))

    (define (%run-group name body-thunk cleanup-thunk)
      (if (%group-should-skip? name)
          (begin
            (test-begin name)
            (test-runner-skip-count! (test-runner-current)
                                      (+ 1 (test-runner-skip-count (test-runner-current))))
            (test-end name))
          (dynamic-wind
            (lambda () (test-begin name))
            body-thunk
            (lambda ()
              (guard (e (#t #f)) (cleanup-thunk))
              (test-end name)))))

    (define-syntax test-group-with-cleanup
      (syntax-rules ()
        ((_ suite-name decl ... cleanup)
         (let ((%tg-name suite-name))
           (%run-group %tg-name (lambda () decl ...) (lambda () cleanup))))))

    (define-syntax test-group
      (syntax-rules ()
        ((_ suite-name decl ...)
         (test-group-with-cleanup suite-name decl ... (if #f #f)))))

    ; ── test-apply ──────────────────────────────────────────────────────

    (define (test-apply . args)
      (let* ((rargs (reverse args))
             (proc (car rargs))
             (rest (reverse (cdr rargs)))
             (given-runner (if (and (pair? rest) (test-runner? (car rest))) (car rest) #f))
             (specs (if given-runner (cdr rest) rest))
             (runner (or given-runner (test-runner-current))))
        (test-with-runner runner
          (if (null? specs)
              (proc)
              (let ((combined (apply test-match-any specs)))
                (test-skip (lambda (name) (not (combined name))))
                (proc))))))))
