;;; SRFI-224: Integer Mappings.
;;;
;;; https://srfi.schemers.org/srfi-224/ -- fxmappings: finite, immutable
;;; maps from exact-integer ("fixnum") keys to arbitrary values, with a
;;; large API (construction, update, whole-mapping queries, traversal,
;;; filtering, set-theoretic combination, and sorted-key interval
;;; queries) that SRFI-146's own general mapping type also has, here
;;; specialized (and, per the SRFI's own text, optimized) for integer
;;; keys specifically.
;;;
;;; No fixnum-range restriction: curry's numeric tower promotes
;;; fixnums to bignums transparently with no user-visible distinction
;;; (see CLAUDE.md's numeric-tower section) and exposes no `fixnum?`
;;; predicate of its own -- this library accepts any exact integer as a
;;; key rather than restricting to a specific machine-word range, which
;;; is strictly more permissive than the spec requires, never less.
;;;
;;; Representation: a sorted (ascending by key), immutable association
;;; list, wrapped in an opaque record -- NOT the sample implementation's
;;; own big-endian Patricia (radix) tree. Every operation here is
;;; correct and every ordering/traversal guarantee the spec makes is
;;; honored exactly; the asymptotic complexity is O(n) per operation
;;; rather than the reference's O(min(n, W)) (W = word width), a
;;; deliberate, documented tradeoff (see docs/reference/srfi/s224.md)
;;; matching this codebase's own established practice of shipping a
;;; correct, straightforward implementation first (see e.g. (srfi 132)'s
;;; sorting procedures, which are "just" a stable merge sort even where
;;; the spec permits something asymptotically fancier) rather than
;;; deferring a whole SRFI on a from-scratch Patricia-tree
;;; implementation.
;;;
;;; fxmapping-alter is the one genuine "workhorse" primitive (mirroring
;;; (srfi s225 dictionaries)'s own dict-find-update! design exactly,
;;; down to the shape of its four inner continuations): every other
;;; single-key update (adjoin, set, delete, adjust, update) is expressed
;;; as one fxmapping-alter call whose failure/success callbacks choose
;;; which continuation to invoke.

(define-library (srfi s224 fxmappings)
  (import (scheme base) (srfi s128 comparators) (srfi s158 generators-and-accumulators))
  (export
    ;; Type
    fxmapping?
    ;; Constructors
    fxmapping fxmapping-unfold fxmapping-accumulate
    alist->fxmapping alist->fxmapping/combinator
    ;; Predicates
    fxmapping-contains? fxmapping-empty? fxmapping-disjoint?
    ;; Accessors
    fxmapping-ref fxmapping-ref/default fxmapping-min fxmapping-max
    ;; Updaters
    fxmapping-adjoin fxmapping-adjoin/combinator fxmapping-set fxmapping-adjust
    fxmapping-delete fxmapping-delete-all fxmapping-update fxmapping-alter
    fxmapping-delete-min fxmapping-delete-max
    fxmapping-update-min fxmapping-update-max
    fxmapping-pop-min fxmapping-pop-max
    ;; Whole mapping
    fxmapping-size fxmapping-find fxmapping-count fxmapping-any? fxmapping-every?
    ;; Traversal
    fxmapping-map fxmapping-for-each fxmapping-fold fxmapping-fold-right
    fxmapping-map->list fxmapping-relation-map
    ;; Filtering
    fxmapping-filter fxmapping-remove fxmapping-partition
    ;; Conversion
    fxmapping->alist fxmapping->decreasing-alist fxmapping-keys fxmapping-values
    fxmapping->generator fxmapping->decreasing-generator
    ;; Comparison
    fxmapping=? fxmapping<? fxmapping<=? fxmapping>? fxmapping>=?
    ;; Set-theoretic
    fxmapping-union fxmapping-intersection fxmapping-difference fxmapping-xor
    fxmapping-union/combinator fxmapping-intersection/combinator
    ;; Submappings / intervals
    fxmapping-open-interval fxmapping-closed-interval
    fxmapping-open-closed-interval fxmapping-closed-open-interval
    fxsubmapping= fxsubmapping< fxsubmapping<= fxsubmapping> fxsubmapping>=
    fxmapping-split)
  (begin

    ;; ── Representation ───────────────────────────────────────────────────────

    (define-record-type <fxmapping> (%make-fxmapping alist) fxmapping? (alist %fxm-alist))

    (define (%check-key k who)
      (if (not (and (integer? k) (exact? k)))
          (error (string-append who ": key must be an exact integer") k)))

    ;; ── Sorted-alist primitives (all pure; every result is a NEW list) ──────

    (define (%alist-find alist k)
      (cond ((null? alist) #f)
            ((= (caar alist) k) (car alist))
            ((> (caar alist) k) #f)
            (else (%alist-find (cdr alist) k))))

    (define (%alist-set alist k v)
      (cond ((null? alist) (list (cons k v)))
            ((= (caar alist) k) (cons (cons k v) (cdr alist)))
            ((> (caar alist) k) (cons (cons k v) alist))
            (else (cons (car alist) (%alist-set (cdr alist) k v)))))

    (define (%alist-adjoin alist k v)
      (cond ((null? alist) (list (cons k v)))
            ((= (caar alist) k) alist)
            ((> (caar alist) k) (cons (cons k v) alist))
            (else (cons (car alist) (%alist-adjoin (cdr alist) k v)))))

    ;; Shared by alist->fxmapping/combinator and fxmapping-adjoin/
    ;; combinator (union/combinator and intersection/combinator use
    ;; their own separate %alist-union-combine/%alist-intersection-
    ;; combine below, with a different argument order -- this comment
    ;; previously, incorrectly, claimed they shared this helper too).
    ;; combine is called as (combine k NEW OLD) -- the just-supplied
    ;; value first, the already-present one second -- per the spec's
    ;; own text and worked example for both call sites ("(proc k v2
    ;; v1)"/"(f k v2 v1)", v2 being the newer value): a real bug, found
    ;; by review, in an earlier draft of this file called combine as
    ;; (k old new), silently wrong for any non-commutative combiner
    ;; (string-append, cons, subtraction, ...).
    (define (%alist-combine alist k v combine)
      (cond ((null? alist) (list (cons k v)))
            ((= (caar alist) k) (cons (cons k (combine k v (cdar alist))) (cdr alist)))
            ((> (caar alist) k) (cons (cons k v) alist))
            (else (cons (car alist) (%alist-combine (cdr alist) k v combine)))))

    (define (%alist-delete alist k)
      (cond ((null? alist) alist)
            ((= (caar alist) k) (cdr alist))
            ((> (caar alist) k) alist)
            (else (cons (car alist) (%alist-delete (cdr alist) k)))))

    (define (%alist-filter pred alist)
      (cond ((null? alist) '())
            ((pred (caar alist) (cdar alist)) (cons (car alist) (%alist-filter pred (cdr alist))))
            (else (%alist-filter pred (cdr alist)))))

    (define (%last-pair a) (if (null? (cdr a)) (car a) (%last-pair (cdr a))))
    (define (%all-but-last a) (if (null? (cdr a)) '() (cons (car a) (%all-but-last (cdr a)))))

    (define (%alist-union a b)
      (cond ((null? a) b) ((null? b) a)
            ((= (caar a) (caar b)) (cons (car a) (%alist-union (cdr a) (cdr b))))
            ((< (caar a) (caar b)) (cons (car a) (%alist-union (cdr a) b)))
            (else (cons (car b) (%alist-union a (cdr b))))))
    (define (%alist-union-combine a b combine)
      (cond ((null? a) b) ((null? b) a)
            ((= (caar a) (caar b))
             (cons (cons (caar a) (combine (caar a) (cdar a) (cdar b))) (%alist-union-combine (cdr a) (cdr b) combine)))
            ((< (caar a) (caar b)) (cons (car a) (%alist-union-combine (cdr a) b combine)))
            (else (cons (car b) (%alist-union-combine a (cdr b) combine)))))
    (define (%alist-intersection a b)
      (cond ((or (null? a) (null? b)) '())
            ((= (caar a) (caar b)) (cons (car a) (%alist-intersection (cdr a) (cdr b))))
            ((< (caar a) (caar b)) (%alist-intersection (cdr a) b))
            (else (%alist-intersection a (cdr b)))))
    (define (%alist-intersection-combine a b combine)
      (cond ((or (null? a) (null? b)) '())
            ((= (caar a) (caar b))
             (cons (cons (caar a) (combine (caar a) (cdar a) (cdar b))) (%alist-intersection-combine (cdr a) (cdr b) combine)))
            ((< (caar a) (caar b)) (%alist-intersection-combine (cdr a) b combine))
            (else (%alist-intersection-combine a (cdr b) combine))))
    (define (%alist-difference a b)
      (cond ((null? a) '()) ((null? b) a)
            ((= (caar a) (caar b)) (%alist-difference (cdr a) (cdr b)))
            ((< (caar a) (caar b)) (cons (car a) (%alist-difference (cdr a) b)))
            (else (%alist-difference a (cdr b)))))
    (define (%alist-xor a b)
      (cond ((null? a) b) ((null? b) a)
            ((= (caar a) (caar b)) (%alist-xor (cdr a) (cdr b)))
            ((< (caar a) (caar b)) (cons (car a) (%alist-xor (cdr a) b)))
            (else (cons (car b) (%alist-xor a (cdr b))))))

    ;; ── Constructors ─────────────────────────────────────────────────────────

    (define (%pairs->sorted-alist kvs who)
      (let loop ((alist '()) (kvs kvs))
        (cond
          ((null? kvs) alist)
          ((null? (cdr kvs)) (error (string-append who ": odd number of key/value arguments")))
          (else (%check-key (car kvs) who)
                (loop (%alist-adjoin alist (car kvs) (cadr kvs)) (cddr kvs))))))

    (define (fxmapping . kvs) (%make-fxmapping (%pairs->sorted-alist kvs "fxmapping")))

    (define (fxmapping-unfold stop? mapper successor . seeds)
      (let loop ((seeds seeds) (alist '()))
        (if (apply stop? seeds)
            (%make-fxmapping alist)
            (call-with-values (lambda () (apply mapper seeds))
              (lambda (k v)
                (%check-key k "fxmapping-unfold")
                (loop (call-with-values (lambda () (apply successor seeds)) list)
                      (%alist-adjoin alist k v)))))))

    ;; proc receives an abort-with-result continuation plus the current
    ;; seeds and must return (values key value new-seed ...). Calling
    ;; abort-with-result, at any point, with any number of arguments
    ;; immediately returns the fxmapping accumulated so far (not
    ;; including the in-progress call that invoked it) as
    ;; fxmapping-accumulate's own FIRST return value, followed by
    ;; whatever arguments abort-with-result was itself given, as
    ;; ADDITIONAL return values -- per the spec's own text and its own
    ;; worked example, which calls it as (abort-with-result 'finished).
    ;; A real bug, found by review: an earlier draft of this file made
    ;; this a zero-argument thunk, which raised a wrong-number-of-
    ;; arguments error on that exact example instead of accepting and
    ;; returning the extra value(s).
    ;; Workaround for a separate, pre-existing curry core bug (filed as
    ;; issue #274): invoking a call/cc continuation with more than one
    ;; value silently drops every value past the first, instead of
    ;; forwarding all of them the way a genuine multiple-values return
    ;; must. Packs the fxmapping and every extra value into a single
    ;; list -- call/cc's own single-value case works correctly -- and
    ;; unpacks via (apply values ...) once outside the escape.
    (define (fxmapping-accumulate proc . seeds)
      (apply values
        (call-with-current-continuation
          (lambda (return)
            (let loop ((seeds seeds) (alist '()))
              (let ((abort (lambda extra (return (cons (%make-fxmapping alist) extra)))))
                (call-with-values (lambda () (apply proc abort seeds))
                  (lambda (k v . new-seeds)
                    (%check-key k "fxmapping-accumulate")
                    (loop new-seeds (%alist-adjoin alist k v))))))))))

    (define (alist->fxmapping alist)
      (%make-fxmapping
        (let loop ((acc '()) (lst alist))
          (if (null? lst) acc
              (begin (%check-key (caar lst) "alist->fxmapping")
                     (loop (%alist-adjoin acc (caar lst) (cdar lst)) (cdr lst)))))))
    (define (alist->fxmapping/combinator proc alist)
      (%make-fxmapping
        (let loop ((acc '()) (lst alist))
          (if (null? lst) acc
              (begin (%check-key (caar lst) "alist->fxmapping/combinator")
                     (loop (%alist-combine acc (caar lst) (cdar lst) proc) (cdr lst)))))))

    ;; ── Predicates ───────────────────────────────────────────────────────────

    (define (fxmapping-contains? fxmap k) (and (%alist-find (%fxm-alist fxmap) k) #t))
    (define (fxmapping-empty? fxmap) (null? (%fxm-alist fxmap)))
    (define (fxmapping-disjoint? fxmap1 fxmap2)
      (let loop ((a (%fxm-alist fxmap1)))
        (or (null? a) (and (not (fxmapping-contains? fxmap2 (caar a))) (loop (cdr a))))))

    ;; ── Accessors ────────────────────────────────────────────────────────────

    (define (fxmapping-ref fxmap k . opt)
      (let ((failure (if (pair? opt) (car opt) (lambda () (error "fxmapping-ref: key not found" k))))
            (success (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) values)))
        (let ((found (%alist-find (%fxm-alist fxmap) k)))
          (if found (success (cdr found)) (failure)))))
    (define (fxmapping-ref/default fxmap k default)
      (let ((found (%alist-find (%fxm-alist fxmap) k))) (if found (cdr found) default)))

    (define (fxmapping-min fxmap)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-min: empty fxmapping") (values (caar a) (cdar a)))))
    (define (fxmapping-max fxmap)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-max: empty fxmapping")
            (let ((lp (%last-pair a))) (values (car lp) (cdr lp))))))

    ;; ── Updaters ─────────────────────────────────────────────────────────────

    (define (fxmapping-alter fxmap k failure success)
      (%check-key k "fxmapping-alter")
      (let* ((alist (%fxm-alist fxmap)) (found (%alist-find alist k)))
        (if found
            (success k (cdr found)
              (lambda (new-v) (%make-fxmapping (%alist-set alist k new-v)))
              (lambda () (%make-fxmapping (%alist-delete alist k))))
            (failure
              (lambda (v) (%make-fxmapping (%alist-set alist k v)))
              (lambda () fxmap)))))

    (define (fxmapping-adjoin fxmap . kvs)
      (let loop ((m fxmap) (kvs kvs))
        (if (null? kvs) m
            (loop (fxmapping-alter m (car kvs)
                    (lambda (insert ignore) (insert (cadr kvs)))
                    (lambda (key v replace delete) (replace v)))
                  (cddr kvs)))))
    (define (fxmapping-adjoin/combinator fxmap proc . kvs)
      (%make-fxmapping
        (let loop ((alist (%fxm-alist fxmap)) (kvs kvs))
          (if (null? kvs) alist
              (loop (%alist-combine alist (car kvs) (cadr kvs) proc) (cddr kvs))))))
    (define (fxmapping-set fxmap . kvs)
      (let loop ((m fxmap) (kvs kvs))
        (if (null? kvs) m
            (loop (fxmapping-alter m (car kvs)
                    (lambda (insert ignore) (insert (cadr kvs)))
                    (lambda (key v replace delete) (replace (cadr kvs))))
                  (cddr kvs)))))
    (define (fxmapping-adjust fxmap k proc)
      (fxmapping-alter fxmap k
        (lambda (insert ignore) (ignore))
        (lambda (key v replace delete) (replace (proc key v)))))
    (define (fxmapping-delete fxmap . ks)
      (let loop ((m fxmap) (ks ks))
        (if (null? ks) m
            (loop (fxmapping-alter m (car ks)
                    (lambda (insert ignore) (ignore))
                    (lambda (key v replace delete) (delete)))
                  (cdr ks)))))
    (define (fxmapping-delete-all fxmap ks) (apply fxmapping-delete fxmap ks))

    ;; fxmapping-update's own failure is a bare thunk that may return
    ;; ANY value directly (not itself routed through insert/ignore the
    ;; way fxmapping-alter's failure is) -- implemented directly against
    ;; the alist rather than delegating to fxmapping-alter, since the
    ;; two failure-callback shapes are genuinely different, not just
    ;; differently-named.
    (define (fxmapping-update fxmap k proc . opt)
      (%check-key k "fxmapping-update")
      (let* ((alist (%fxm-alist fxmap)) (found (%alist-find alist k)))
        (if found
            (proc k (cdr found)
              (lambda (new-v) (%make-fxmapping (%alist-set alist k new-v)))
              (lambda () (%make-fxmapping (%alist-delete alist k))))
            (if (pair? opt) ((car opt)) (error "fxmapping-update: key not found" k)))))

    (define (fxmapping-delete-min fxmap)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-delete-min: empty fxmapping") (%make-fxmapping (cdr a)))))
    (define (fxmapping-delete-max fxmap)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-delete-max: empty fxmapping") (%make-fxmapping (%all-but-last a)))))

    (define (fxmapping-update-min fxmap proc)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-update-min: empty fxmapping")
            (proc (caar a) (cdar a)
              (lambda (nv) (%make-fxmapping (cons (cons (caar a) nv) (cdr a))))
              (lambda () (%make-fxmapping (cdr a)))))))
    (define (fxmapping-update-max fxmap proc)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-update-max: empty fxmapping")
            (let* ((lp (%last-pair a)) (k (car lp)) (v (cdr lp)) (rest (%all-but-last a)))
              (proc k v
                (lambda (nv) (%make-fxmapping (append rest (list (cons k nv)))))
                (lambda () (%make-fxmapping rest)))))))

    (define (fxmapping-pop-min fxmap)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-pop-min: empty fxmapping")
            (values (caar a) (cdar a) (%make-fxmapping (cdr a))))))
    (define (fxmapping-pop-max fxmap)
      (let ((a (%fxm-alist fxmap)))
        (if (null? a) (error "fxmapping-pop-max: empty fxmapping")
            (let ((lp (%last-pair a)))
              (values (car lp) (cdr lp) (%make-fxmapping (%all-but-last a)))))))

    ;; ── Whole mapping ────────────────────────────────────────────────────────

    (define (fxmapping-size fxmap) (length (%fxm-alist fxmap)))
    (define (fxmapping-find pred fxmap failure . opt)
      (let ((success (if (pair? opt) (car opt) values)))
        (let loop ((a (%fxm-alist fxmap)))
          (cond ((null? a) (failure))
                ((pred (caar a) (cdar a)) (success (caar a) (cdar a)))
                (else (loop (cdr a)))))))
    (define (fxmapping-count pred fxmap) (fxmapping-fold (lambda (k v acc) (if (pred k v) (+ acc 1) acc)) 0 fxmap))
    (define (fxmapping-any? pred fxmap)
      (let loop ((a (%fxm-alist fxmap))) (and (pair? a) (or (pred (caar a) (cdar a)) (loop (cdr a))))))
    (define (fxmapping-every? pred fxmap)
      (let loop ((a (%fxm-alist fxmap))) (or (null? a) (and (pred (caar a) (cdar a)) (loop (cdr a))))))

    ;; ── Traversal ────────────────────────────────────────────────────────────
    ;; fxmapping-map's own dynamic application order is explicitly
    ;; unspecified per the SRFI's own text -- unlike (srfi 225)'s own
    ;; dict-for-each (which needed a strictly-sequential substitute for
    ;; exactly this reason), plain `map` (which curry auto-parallelizes
    ;; above 8 elements) is safe and spec-conformant to use directly
    ;; here, with no ordering workaround needed.

    (define (fxmapping-map proc fxmap)
      (%make-fxmapping (map (lambda (kv) (cons (car kv) (proc (car kv) (cdr kv)))) (%fxm-alist fxmap))))
    (define (fxmapping-for-each proc fxmap)
      (for-each (lambda (kv) (proc (car kv) (cdr kv))) (%fxm-alist fxmap))
      (if #f #f))
    (define (fxmapping-fold kons knil fxmap)
      (let loop ((a (%fxm-alist fxmap)) (acc knil))
        (if (null? a) acc (loop (cdr a) (kons (caar a) (cdar a) acc)))))
    (define (fxmapping-fold-right kons knil fxmap)
      (let loop ((a (%fxm-alist fxmap)))
        (if (null? a) knil (kons (caar a) (cdar a) (loop (cdr a))))))
    (define (fxmapping-map->list proc fxmap) (map (lambda (kv) (proc (car kv) (cdr kv))) (%fxm-alist fxmap)))
    ;; Behavior is explicitly "unpredictable" (the spec's own word) for a
    ;; non-injective proc; %alist-set's overwrite-on-collision behavior
    ;; is one reasonable, non-crashing resolution among the several the
    ;; spec itself declines to pin down.
    (define (fxmapping-relation-map proc fxmap)
      (%make-fxmapping
        (let loop ((a (%fxm-alist fxmap)) (acc '()))
          (if (null? a) acc
              (call-with-values (lambda () (proc (caar a) (cdar a)))
                (lambda (nk nv) (loop (cdr a) (%alist-set acc nk nv))))))))

    ;; ── Filtering ────────────────────────────────────────────────────────────

    (define (fxmapping-filter pred fxmap) (%make-fxmapping (%alist-filter pred (%fxm-alist fxmap))))
    (define (fxmapping-remove pred fxmap) (%make-fxmapping (%alist-filter (lambda (k v) (not (pred k v))) (%fxm-alist fxmap))))
    (define (fxmapping-partition pred fxmap) (values (fxmapping-filter pred fxmap) (fxmapping-remove pred fxmap)))

    ;; ── Conversion ───────────────────────────────────────────────────────────

    (define (fxmapping->alist fxmap) (map (lambda (kv) (cons (car kv) (cdr kv))) (%fxm-alist fxmap)))
    (define (fxmapping->decreasing-alist fxmap) (reverse (fxmapping->alist fxmap)))
    (define (fxmapping-keys fxmap) (map car (%fxm-alist fxmap)))
    (define (fxmapping-values fxmap) (map cdr (%fxm-alist fxmap)))
    (define (fxmapping->generator fxmap) (list->generator (fxmapping->alist fxmap)))
    (define (fxmapping->decreasing-generator fxmap) (list->generator (fxmapping->decreasing-alist fxmap)))

    ;; ── Comparison ───────────────────────────────────────────────────────────
    ;; Keys always compared with =; comp (a SRFI-128 comparator) supplies
    ;; value equality only.

    (define (%fxm-subset? comp fxmap1 fxmap2)
      (let ((eq (comparator-equality-predicate comp)))
        (let loop ((a (%fxm-alist fxmap1)))
          (or (null? a)
              (let ((found (%alist-find (%fxm-alist fxmap2) (caar a))))
                (and found (eq (cdar a) (cdr found)) (loop (cdr a))))))))

    (define (fxmapping=? comp . fxmaps)
      (or (null? fxmaps) (null? (cdr fxmaps))
          (and (= (fxmapping-size (car fxmaps)) (fxmapping-size (cadr fxmaps)))
               (%fxm-subset? comp (car fxmaps) (cadr fxmaps))
               (apply fxmapping=? comp (cdr fxmaps)))))
    (define (fxmapping<=? comp . fxmaps)
      (or (null? fxmaps) (null? (cdr fxmaps))
          (and (%fxm-subset? comp (car fxmaps) (cadr fxmaps))
               (apply fxmapping<=? comp (cdr fxmaps)))))
    (define (fxmapping<? comp . fxmaps)
      (or (null? fxmaps) (null? (cdr fxmaps))
          (and (< (fxmapping-size (car fxmaps)) (fxmapping-size (cadr fxmaps)))
               (%fxm-subset? comp (car fxmaps) (cadr fxmaps))
               (apply fxmapping<? comp (cdr fxmaps)))))
    ;; "A >= B >= C" is exactly "C <= B <= A" -- reversing the whole
    ;; chain and re-checking <=?/<? along it is correct for any length,
    ;; not just two arguments.
    (define (fxmapping>=? comp . fxmaps) (apply fxmapping<=? comp (reverse fxmaps)))
    (define (fxmapping>? comp . fxmaps) (apply fxmapping<? comp (reverse fxmaps)))

    ;; ── Set-theoretic operations ─────────────────────────────────────────────

    (define (fxmapping-union fxmap1 . fxmaps)
      (%make-fxmapping
        (let loop ((acc (%fxm-alist fxmap1)) (rest fxmaps))
          (if (null? rest) acc (loop (%alist-union acc (%fxm-alist (car rest))) (cdr rest))))))
    (define (fxmapping-intersection fxmap1 . fxmaps)
      (%make-fxmapping
        (let loop ((acc (%fxm-alist fxmap1)) (rest fxmaps))
          (if (null? rest) acc (loop (%alist-intersection acc (%fxm-alist (car rest))) (cdr rest))))))
    (define (fxmapping-difference fxmap1 . fxmaps)
      (%make-fxmapping
        (let loop ((acc (%fxm-alist fxmap1)) (rest fxmaps))
          (if (null? rest) acc (loop (%alist-difference acc (%fxm-alist (car rest))) (cdr rest))))))
    (define (fxmapping-xor fxmap1 fxmap2) (%make-fxmapping (%alist-xor (%fxm-alist fxmap1) (%fxm-alist fxmap2))))
    (define (fxmapping-union/combinator proc fxmap1 . fxmaps)
      (%make-fxmapping
        (let loop ((acc (%fxm-alist fxmap1)) (rest fxmaps))
          (if (null? rest) acc (loop (%alist-union-combine acc (%fxm-alist (car rest)) proc) (cdr rest))))))
    (define (fxmapping-intersection/combinator proc fxmap1 . fxmaps)
      (%make-fxmapping
        (let loop ((acc (%fxm-alist fxmap1)) (rest fxmaps))
          (if (null? rest) acc (loop (%alist-intersection-combine acc (%fxm-alist (car rest)) proc) (cdr rest))))))

    ;; ── Submappings / interval queries ───────────────────────────────────────

    (define (fxmapping-open-interval fxmap lo hi) (fxmapping-filter (lambda (k v) (and (> k lo) (< k hi))) fxmap))
    (define (fxmapping-closed-interval fxmap lo hi) (fxmapping-filter (lambda (k v) (and (>= k lo) (<= k hi))) fxmap))
    (define (fxmapping-open-closed-interval fxmap lo hi) (fxmapping-filter (lambda (k v) (and (> k lo) (<= k hi))) fxmap))
    (define (fxmapping-closed-open-interval fxmap lo hi) (fxmapping-filter (lambda (k v) (and (>= k lo) (< k hi))) fxmap))
    (define (fxsubmapping= fxmap k) (fxmapping-filter (lambda (kk v) (= kk k)) fxmap))
    (define (fxsubmapping< fxmap k) (fxmapping-filter (lambda (kk v) (< kk k)) fxmap))
    (define (fxsubmapping<= fxmap k) (fxmapping-filter (lambda (kk v) (<= kk k)) fxmap))
    (define (fxsubmapping> fxmap k) (fxmapping-filter (lambda (kk v) (> kk k)) fxmap))
    (define (fxsubmapping>= fxmap k) (fxmapping-filter (lambda (kk v) (>= kk k)) fxmap))
    (define (fxmapping-split fxmap k) (values (fxsubmapping<= fxmap k) (fxsubmapping> fxmap k)))))
