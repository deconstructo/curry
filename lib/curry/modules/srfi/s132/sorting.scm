(define-library (srfi s132 sorting)
  (import (scheme base))
  (export
    list-sorted? vector-sorted?
    list-sort list-stable-sort list-sort! list-stable-sort!
    vector-sort vector-sort! vector-stable-sort vector-stable-sort!
    list-merge list-merge! vector-merge vector-merge!)
  (begin

    ; Every sort here is a stable merge sort — SRFI-132 permits list-sort/
    ; vector-sort to be unstable, but there's no benefit to a second,
    ; less-predictable algorithm, so the "plain" and "stable" names are
    ; aliases of the same implementation.

    (define (list-sorted? less? lst)
      (or (null? lst) (null? (cdr lst))
          (and (not (less? (cadr lst) (car lst)))
               (list-sorted? less? (cdr lst)))))

    (define (vector-sorted? less? v . range)
      (let ((start (if (pair? range) (car range) 0))
            (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v))))
        (let loop ((i (+ start 1)))
          (or (>= i end) (and (not (less? (vector-ref v i) (vector-ref v (- i 1)))) (loop (+ i 1)))))))

    ;; Accumulator-based tail loop, not the textbook non-tail cons-then-
    ;; recurse shape -- SRFI 14's char-set construction (%normalize-ranges)
    ;; calls this on large sorted ranges, and once list-merge is compiled
    ;; against curry's VM (a define-library body no longer tree-walked,
    ;; see the eval-elimination migration) the VM's 256-frame call-stack
    ;; guard is much stricter than the tree-walker's own budget was --
    ;; the straightforward non-tail version legitimately overflowed on
    ;; real char-set data, not just pathological input. Same fix pattern
    ;; already applied to (srfi 1)/(srfi 160)'s own non-tail cons-builders.
    ;;
    ;; Deliberately ONE textual call to `loop`, not two (the natural way
    ;; to write this compares, then calls loop differently in each
    ;; branch) -- confirmed via a minimal, define-library-independent
    ;; repro (reproduces at plain top level too, nothing to do with
    ;; target_env/library compilation) that curry's compiler/VM does not
    ;; correctly tail-call-optimize a named let with two or more
    ;; DIFFERENT self-recursive call sites in the same function: each
    ;; call genuinely pushes a fresh VM frame instead of reusing the
    ;; current one, so real (non-degenerate -- i.e. both lists actually
    ;; interleaving, not one side staying empty) input overflows the
    ;; 256-frame guard once total comparisons exceed it. This is a real,
    ;; separate, pre-existing VM/compiler bug, not a symptom of this
    ;; migration -- worth its own investigation later, not attempted
    ;; here. Computing the branch choice once and making a single
    ;; parameterized tail call sidesteps it.
    (define (list-merge less? a b)
      (let loop ((a a) (b b) (acc '()))
        (cond
          ((null? a) (%rev-onto acc b))
          ((null? b) (%rev-onto acc a))
          (else
            (let ((take-b? (less? (car b) (car a))))
              (loop (if take-b? a (cdr a))
                    (if take-b? (cdr b) b)
                    (cons (if take-b? (car b) (car a)) acc)))))))

    (define (%rev-onto acc lst)
      (if (null? acc) lst (%rev-onto (cdr acc) (cons (car acc) lst))))

    (define list-merge! list-merge)

    ;; Returns a pair (front . back), not multiple values -- call-with-
    ;; values wrapping a receiver that itself makes further (non-tail)
    ;; recursive calls is a known, still-open curry VM TCO gap (see
    ;; project memory / docs/thoughts on the eval-elimination migration):
    ;; each call-with-values invocation leaks a stack frame that's never
    ;; reclaimed until the whole outer call returns, rather than being
    ;; genuinely O(1). That's invisible for a single split/merge, but
    ;; list-stable-sort calls %list-split + list-merge once per node of
    ;; its recursion tree -- thousands of times merging a few-thousand-
    ;; element list -- so the leaked frames from call-with-values
    ;; accumulate across the WHOLE sort, not just its O(log n) tree
    ;; depth, and blow the VM's 256-frame guard on real input sizes once
    ;; this library is VM-compiled rather than tree-walked. Returning a
    ;; plain pair and destructuring with car/cdr sidesteps call-with-
    ;; values entirely -- same fix shape already used elsewhere in this
    ;; codebase (SRFI 160's TAGvector-unfold) for the identical defect.
    (define (%list-split lst)
      (let loop ((slow lst) (fast lst) (acc '()))
        (if (or (null? fast) (null? (cdr fast)))
            (cons (reverse acc) slow)
            (loop (cdr slow) (cddr fast) (cons (car slow) acc)))))

    (define (list-stable-sort less? lst)
      (if (or (null? lst) (null? (cdr lst)))
          lst
          (let ((split (%list-split lst)))
            (list-merge less? (list-stable-sort less? (car split)) (list-stable-sort less? (cdr split))))))

    (define list-sort list-stable-sort)
    (define list-sort! list-stable-sort)
    (define list-stable-sort! list-stable-sort)

    (define (vector-stable-sort less? v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v)))
             (sorted (list-stable-sort less? (vector->list v start end)))
             (out (vector-copy v)))
        (let loop ((i start) (l sorted))
          (if (pair? l) (begin (vector-set! out i (car l)) (loop (+ i 1) (cdr l)))))
        out))

    (define (vector-stable-sort! less? v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v)))
             (sorted (list-stable-sort less? (vector->list v start end))))
        (let loop ((i start) (l sorted))
          (if (pair? l) (begin (vector-set! v i (car l)) (loop (+ i 1) (cdr l)))))
        v))

    (define vector-sort vector-stable-sort)
    (define vector-sort! vector-stable-sort!)

    (define (vector-merge less? a b)
      (list->vector (list-merge less? (vector->list a) (vector->list b))))

    (define (vector-merge! less? to a b)
      (let loop ((i 0) (l (list-merge less? (vector->list a) (vector->list b))))
        (if (pair? l) (begin (vector-set! to i (car l)) (loop (+ i 1) (cdr l)))))
      to)))
