;;; (curry ros) — a client for the rosbridge v2.0 JSON protocol
;;; (https://github.com/RobotWebTools/rosbridge_suite), letting curry talk
;;; to a running ROS1 or ROS2 system through `rosbridge_server` without any
;;; ROS client library or DDS transport linked into curry itself. Built
;;; entirely on (curry websocket) + (curry json) + (curry sync) + curry's
;;; actor system -- no new C code.
;;;
;;; A single background actor ("the reader") owns the WebSocket connection
;;; on the read side: it decodes each incoming frame as JSON and dispatches
;;; by the message's "op" field -- publish messages go to that topic's
;;; registered callback(s), service_response messages wake up the blocked
;;; ros-call-service that's waiting on that id, and call_service messages
;;; (the server invoking a service *we* advertised) get dispatched to the
;;; handler passed to ros-advertise-service!. All of the connection's
;;; mutable bookkeeping (subscriptions, pending calls, advertised
;;; services, the id counter) is guarded by one mutex, since publisher
;;; actors and the reader actor touch it concurrently.
;;;
;;; Callbacks (ros-subscribe!, ros-advertise-service!) run directly on the
;;; reader actor -- keep them fast, or dispatch onward via `send!` to a
;;; separate actor, the same pattern (curry sync)'s own docs recommend for
;;; any callback-style API here.

(define-library (curry ros)
  (import (scheme base) (curry websocket) (curry json) (curry sync))
  (export
    ros-connect ros-close! ros-connected?
    ros-advertise! ros-unadvertise! ros-publish!
    ros-subscribe! ros-unsubscribe!
    ros-call-service ros-advertise-service! ros-unadvertise-service!)
  (begin

    (define-record-type <ros-conn>
      (%make-ros-conn ws lock next-id subscriptions pending-calls services reader)
      ros-conn?
      (ws            %ros-ws)
      (lock          %ros-lock)
      (next-id       %ros-next-id %ros-set-next-id!)
      (subscriptions %ros-subscriptions)  ; topic string -> list of callbacks
      (pending-calls %ros-pending-calls)  ; id string -> <pending-call>
      (services      %ros-services)       ; service string -> handler procedure
      (reader        %ros-reader %ros-set-reader!))

    (define-record-type <pending-call>
      (%make-pending mutex condvar done? result-ok? result-values)
      pending-call?
      (mutex   pending-mutex)
      (condvar pending-condvar)
      (done?   pending-done? set-pending-done?!)
      (result-ok? pending-result-ok? set-pending-result-ok?!)
      (result-values pending-result-values set-pending-result-values!))

    (define (%with-lock conn thunk) (with-mutex (%ros-lock conn) thunk))

    (define (%next-id! conn)
      (%with-lock conn
        (lambda ()
          (let ((n (%ros-next-id conn)))
            (%ros-set-next-id! conn (+ n 1))
            (string-append "curry:" (number->string n))))))

    (define (ros-connected? conn) (not (ws-closed? (%ros-ws conn))))

    ;; ---- connecting and the reader loop

    (define (ros-connect url)
      (let* ((ws (ws-connect url))
             (conn (%make-ros-conn ws (make-mutex) 0
                                   (make-hash-table) (make-hash-table)
                                   (make-hash-table) #f)))
        (%ros-set-reader! conn (spawn (lambda () (%reader-loop conn))))
        conn))

    (define (ros-close! conn)
      (ws-close! (%ros-ws conn))
      (%fail-all-pending! conn))

    ;; Wakes every ros-call-service currently blocked on this connection
    ;; with a failure result, instead of leaving them hanging forever.
    ;; Needed both when the user calls ros-close! and when the reader
    ;; actor observes the peer closing the connection out from under us
    ;; -- neither of those otherwise touches pending-calls or its
    ;; per-call condvars at all.
    (define (%fail-all-pending! conn)
      (let ((pendings (%with-lock conn
                        (lambda ()
                          (let* ((table (%ros-pending-calls conn))
                                 (ps (hash-table-values table)))
                            (for-each (lambda (k) (hash-table-delete! table k)) (hash-table-keys table))
                            ps)))))
        (for-each
         (lambda (pending)
           (with-mutex (pending-mutex pending)
             (lambda ()
               (if (not (pending-done? pending))
                   (begin
                     (set-pending-result-ok?! pending #f)
                     (set-pending-result-values! pending '())
                     (set-pending-done?! pending #t)
                     (cond-broadcast! (pending-condvar pending)))))))
         pendings)))

    (define (%reader-loop conn)
      (let ((msg (ws-recv! (%ros-ws conn))))
        (if (eof-object? msg)
            (%fail-all-pending! conn) ; connection closed -- reader exits
            (begin
              (%dispatch! conn (%decode-message msg))
              (%reader-loop conn)))))

    ;; ws-recv! only ever returns a string here (rosbridge never sends
    ;; binary frames in JSON mode); a malformed/non-JSON frame is dropped
    ;; rather than killing the reader actor, which would otherwise take
    ;; every subscription and pending call down silently with it.
    (define (%decode-message msg)
      (guard (e (#t #f))
        (if (string? msg) (json-parse msg) #f)))

    (define (%dispatch! conn parsed)
      (if parsed
          (let ((op (%field parsed "op")))
            (cond
              ((equal? op "publish") (%dispatch-publish! conn parsed))
              ((equal? op "service_response") (%dispatch-service-response! conn parsed))
              ((equal? op "call_service") (%dispatch-call-service! conn parsed))
              ((equal? op "status")
               (display "(curry ros): status from server: ")
               (display (%field parsed "msg"))
               (newline))
              (else #t)))))

    (define (%field alist name)
      (let ((p (assoc name alist))) (if p (cdr p) #f)))

    (define (%dispatch-publish! conn parsed)
      (let* ((topic (%field parsed "topic"))
             (payload (%field parsed "msg"))
             (callbacks (%with-lock conn
                          (lambda () (hash-table-ref (%ros-subscriptions conn) topic '())))))
        (for-each (lambda (cb) (cb payload)) callbacks)))

    (define (%dispatch-service-response! conn parsed)
      (let* ((id (%field parsed "id"))
             (pending (and id (%with-lock conn
                                (lambda () (hash-table-ref (%ros-pending-calls conn) id #f))))))
        (if pending
            (with-mutex (pending-mutex pending)
              (lambda ()
                (set-pending-result-ok?! pending (%field parsed "result"))
                (set-pending-result-values! pending (or (%field parsed "values") '()))
                (set-pending-done?! pending #t)
                (cond-broadcast! (pending-condvar pending)))))))

    (define (%dispatch-call-service! conn parsed)
      (let* ((service (%field parsed "service"))
             (id (%field parsed "id"))
             (args (or (%field parsed "args") '()))
             (handler (%with-lock conn
                        (lambda () (hash-table-ref (%ros-services conn) service #f)))))
        (if handler
            (call-with-values
             (lambda () (handler args))
             (lambda (ok? values)
               (%send! conn
                       (append (list (cons "op" "service_response")
                                     (cons "service" service)
                                     (cons "values" values)
                                     (cons "result" ok?))
                               (if id (list (cons "id" id)) '()))))))))

    (define (%send! conn alist) (ws-send! (%ros-ws conn) (json-stringify alist)))

    ;; ---- topics

    (define (ros-advertise! conn topic type)
      (%send! conn (list (cons "op" "advertise") (cons "topic" topic) (cons "type" type))))

    (define (ros-unadvertise! conn topic)
      (%send! conn (list (cons "op" "unadvertise") (cons "topic" topic))))

    (define (ros-publish! conn topic msg)
      (%send! conn (list (cons "op" "publish") (cons "topic" topic) (cons "msg" msg))))

    ;; Multiple ros-subscribe! calls on the same topic add independent
    ;; callbacks (all get invoked), but only the first one actually sends
    ;; a "subscribe" op to the server -- rosbridge itself is fine with
    ;; duplicate subscribes, but there's no reason to ask twice.
    (define (ros-subscribe! conn topic callback . type)
      (let ((first? (%with-lock conn
                      (lambda ()
                        (let* ((subs (%ros-subscriptions conn))
                               (existing (hash-table-ref subs topic '())))
                          (hash-table-set! subs topic (append existing (list callback)))
                          (null? existing))))))
        (if first?
            (%send! conn (append (list (cons "op" "subscribe") (cons "topic" topic))
                                  (if (pair? type) (list (cons "type" (car type))) '()))))))

    (define (ros-unsubscribe! conn topic)
      (%with-lock conn (lambda () (hash-table-delete! (%ros-subscriptions conn) topic)))
      (%send! conn (list (cons "op" "unsubscribe") (cons "topic" topic))))

    ;; ---- services

    ;; Blocks the calling thread until the response arrives, the
    ;; connection is closed, or `timeout` seconds elapse (if given);
    ;; returns (values ok? values-list) -- (values #f '()) for a timeout
    ;; or a connection loss while waiting. Safe to call from several
    ;; actors at once -- each call gets its own id and its own
    ;; <pending-call>, so concurrent calls never see each other's
    ;; responses.
    ;;
    ;; cond-wait!/cond-wait-timeout! (like the pthread primitives they
    ;; wrap) may return without the awaited condition actually being
    ;; true -- a "spurious wakeup", explicitly permitted by POSIX, not a
    ;; hypothetical. The loop below re-checks pending-done? and keeps
    ;; waiting rather than treating any single non-done wakeup as a
    ;; terminal failure; for the timeout case, `deadline` is fixed once
    ;; up front and the remaining budget is recomputed on every retry
    ;; rather than restarting a fresh full-length wait each time.
    (define (ros-call-service conn service args . timeout)
      (let* ((id (%next-id! conn))
             (pending (%make-pending (make-mutex) (make-condvar) #f #f '()))
             (deadline (and (pair? timeout) (+ (current-second) (car timeout)))))
        (%with-lock conn (lambda () (hash-table-set! (%ros-pending-calls conn) id pending)))
        ;; ws-send! (inside %send!) raises if the connection has already
        ;; closed -- possible even though we just registered `pending`,
        ;; if the reader actor's connection-loss cleanup already ran and
        ;; exited before we got here (nothing will ever drain/signal this
        ;; entry again in that case). Rather than let the exception
        ;; escape and kill the calling thread, resolve this call as
        ;; failed ourselves immediately so the wait loop below sees
        ;; pending-done? already true and never actually waits.
        (guard (e (#t (with-mutex (pending-mutex pending)
                        (lambda ()
                          (if (not (pending-done? pending))
                              (begin
                                (set-pending-result-ok?! pending #f)
                                (set-pending-result-values! pending '())
                                (set-pending-done?! pending #t)))))))
          (%send! conn (list (cons "op" "call_service") (cons "id" id)
                              (cons "service" service) (cons "args" args))))
        (with-mutex (pending-mutex pending)
          (lambda ()
            (let loop ()
              (if (not (pending-done? pending))
                  (if deadline
                      (let ((remaining (- deadline (current-second))))
                        (if (> remaining 0)
                            (begin
                              (cond-wait-timeout! (pending-condvar pending) (pending-mutex pending) remaining)
                              (loop))
                            #f)) ; deadline genuinely passed -- give up
                      (begin
                        (cond-wait! (pending-condvar pending) (pending-mutex pending))
                        (loop)))))))
        ;; Deletion and the final read happen after releasing
        ;; pending-mutex, not nested inside it -- pending-mutex is never
        ;; held while acquiring conn's own lock, so the two locks can't
        ;; deadlock against each other regardless of what future dispatch
        ;; paths might do.
        (%with-lock conn (lambda () (hash-table-delete! (%ros-pending-calls conn) id)))
        (values (pending-result-ok? pending) (pending-result-values pending))))

    (define (ros-advertise-service! conn service type handler)
      (%with-lock conn (lambda () (hash-table-set! (%ros-services conn) service handler)))
      (%send! conn (list (cons "op" "advertise_service") (cons "type" type) (cons "service" service))))

    (define (ros-unadvertise-service! conn service)
      (%with-lock conn (lambda () (hash-table-delete! (%ros-services conn) service)))
      (%send! conn (list (cons "op" "unadvertise_service") (cons "service" service))))

  )) ;; end begin, define-library
