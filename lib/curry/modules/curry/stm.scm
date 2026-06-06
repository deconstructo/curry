;;; curry/stm — STM and channel sugar layer
;;;
;;; Primitive bindings (always available, no import needed):
;;;   make-tvar  tvar-read  tvar-write!  tvar?  atomically  retry  %or-else
;;;   make-channel  channel-send!  channel-recv!  channel-close!
;;;   channel-closed?  channel?
;;;   %channel-try-send  %channel-try-recv  %channel-blocked?
;;;
;;; This module exports higher-level forms built on top of those primitives.

;;; (or-else e1 e2 ...)
;;; Tries e1 inside an implicit thunk; if it retries, tries e2, etc.
;;; If all alternatives retry, the combined read-set is used to wait.
(define-syntax or-else
  (syntax-rules ()
    ((_ e1 e2)
     (%or-else (lambda () e1) (lambda () e2)))
    ((_ e1 e2 rest ...)
     (%or-else (lambda () e1) (lambda () (or-else e2 rest ...))))))

;;; (select clause ...)
;;;
;;; Each clause is one of:
;;;   (recv ch var body ...)   receive from ch, bind result to var
;;;   (send ch val)            send val to ch
;;;   (else body ...)          fallback if no clause is immediately ready
;;;
;;; Behaviour:
;;;   - Polls all non-else alternatives non-blockingly in order.
;;;   - If one is ready, runs its body and returns.
;;;   - If none is ready and there is an (else ...) clause, runs it.
;;;   - If none is ready and there is no else, calls (retry) — must be
;;;     called inside (atomically ...) to get correct blocking semantics.
(define-syntax select
  (syntax-rules (recv send else)
    ;; Degenerate: single recv — just a direct blocking receive.
    ((_ (recv ch var body ...))
     (let ((var (channel-recv! ch))) body ...))

    ;; Degenerate: single send.
    ((_ (send ch val))
     (channel-send! ch val))

    ;; Degenerate: else only.
    ((_ (else body ...))
     (begin body ...))

    ;; General case.
    ((_ clause ...)
     (let loop ()
       (or (%select-try-clause clause loop) ...
           (%select-fallback loop clause ...))))))

;;; Internal: try one clause non-blockingly.
;;; Returns #f if the channel was not ready; otherwise runs the body.
(define-syntax %select-try-clause
  (syntax-rules (recv send else)
    ((_ (recv ch var body ...) loop)
     (let ((v (%channel-try-recv ch)))
       (if (%channel-blocked? v)
           #f
           (let ((var v)) body ...))))
    ((_ (send ch val) loop)
     (not (%channel-blocked? (%channel-try-send ch val))))
    ((_ (else body ...) loop)
     #f)))

;;; Internal: fallback when no clause was ready.
(define-syntax %select-fallback
  (syntax-rules (else)
    ((_ loop (else body ...) rest ...)  (begin body ...))
    ((_ loop _ rest ...)                (%select-fallback loop rest ...))
    ((_ loop)
     (retry))))
