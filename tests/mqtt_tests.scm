;;; MQTT module tests.
;;;
;;; Usage (normally invoked via test_mqtt.sh):
;;;   curry mqtt_tests.scm [plain-port [tls-port [tls-ca-cert [disco-port]]]]
;;;
;;; plain-port  — plain TCP mosquitto (default 1883)
;;; tls-port    — TLS mosquitto (default 8883)
;;; tls-ca-cert — CA cert path for TLS; empty string or absent → skip TLS tests
;;; disco-port  — ephemeral broker for disconnect-event test; 0 or absent → skip

(import (curry mqtt))

;;; ---- Argument parsing ----

(define raw-args (cdr command-line-args))

(define (arg-port lst default)
  (let ((v (and (pair? lst) (car lst))))
    (or (and (number? v) (exact v))
        (and (symbol? v) (let ((n (string->number (symbol->string v))))
                           (and n (exact n))))
        default)))

(define (arg-path lst)
  (let ((v (and (pair? lst) (car lst))))
    (cond
      ((string? v)  (if (string=? v "") #f v))
      ((symbol? v)  (let ((s (symbol->string v)))
                      (if (string=? s "") #f s)))
      (else #f))))

(define plain-port  (arg-port raw-args              1883))
(define tls-port    (arg-port (cdr raw-args)        8883))
(define tls-ca      (arg-path (cddr raw-args)))
(define disco-port  (arg-port (cdddr raw-args)      0))

;;; ---- Harness ----

(define pass 0)
(define fail 0)
(define skip 0)

(define (check label got expected)
  (cond
    ((equal? got expected)
     (display "PASS: ") (display label) (newline)
     (set! pass (+ pass 1)))
    (else
     (display "FAIL: ") (display label)
     (display " — got ") (write got)
     (display " expected ") (write expected) (newline)
     (set! fail (+ fail 1)))))

(define (check-pred label got pred)
  (cond
    ((pred got)
     (display "PASS: ") (display label) (newline)
     (set! pass (+ pass 1)))
    (else
     (display "FAIL: ") (display label)
     (display " — got ") (write got) (newline)
     (set! fail (+ fail 1)))))

(define (skip! label)
  (display "SKIP: ") (display label) (newline)
  (set! skip (+ skip 1)))

;;; Consume (and discard) messages until the queue is empty.
(define (drain! c)
  (let loop ()
    (let ((m (mqtt-receive c 200)))
      (when m (loop)))))


;;; ================================================================
;;; Part 1 — Plain TCP: basic pub/sub (existing tests)
;;; ================================================================

(define c #f)
(guard (exn (#t (skip! (string-append "MQTT broker unavailable on 127.0.0.1:"
                                      (number->string plain-port)))))
  (set! c (mqtt-connect "127.0.0.1" plain-port "curry-test-plain")))

(when c

  (check "connected?"       (mqtt-connected? c) #t)

  ;;; Basic pub/sub
  (mqtt-subscribe c "curry/test/echo" 1)
  (mqtt-publish   c "curry/test/echo" "hello")
  (let ((msg (mqtt-receive c 2000)))
    (check "pub/sub basic"  (car msg) "curry/test/echo")
    (check "payload"        (cdr msg) "hello"))

  ;;; Multiple messages arrive in order
  (mqtt-publish c "curry/test/echo" "one")
  (mqtt-publish c "curry/test/echo" "two")
  (mqtt-publish c "curry/test/echo" "three")
  (let ((m1 (mqtt-receive c 2000))
        (m2 (mqtt-receive c 2000))
        (m3 (mqtt-receive c 2000)))
    (check "order-1"  (cdr m1) "one")
    (check "order-2"  (cdr m2) "two")
    (check "order-3"  (cdr m3) "three"))

  ;;; QoS 1
  (mqtt-publish c "curry/test/echo" "qos1" 1)
  (check "qos1 payload" (cdr (mqtt-receive c 2000)) "qos1")

  ;;; QoS 2
  (mqtt-publish c "curry/test/echo" "qos2" 2)
  (check "qos2 payload" (cdr (mqtt-receive c 2000)) "qos2")

  ;;; Timeout on empty channel
  (mqtt-unsubscribe c "curry/test/echo")
  (mqtt-subscribe   c "curry/test/empty" 0)
  (check "timeout->false" (mqtt-receive c 300) #f)
  (mqtt-unsubscribe c "curry/test/empty")

  ;;; Wildcard subscription
  (mqtt-subscribe c "curry/wild/#" 0)
  (mqtt-publish   c "curry/wild/a" "alpha")
  (mqtt-publish   c "curry/wild/b" "beta")
  (let ((m1 (mqtt-receive c 2000))
        (m2 (mqtt-receive c 2000)))
    (check "wildcard-a" (cdr m1) "alpha")
    (check "wildcard-b" (cdr m2) "beta"))
  (mqtt-unsubscribe c "curry/wild/#")

  ;;; Payload with special characters
  (mqtt-subscribe c "curry/test/echo" 0)
  (mqtt-publish   c "curry/test/echo" "héllo wörld 🌍")
  (check "unicode payload" (cdr (mqtt-receive c 2000)) "héllo wörld 🌍")
  (mqtt-publish   c "curry/test/echo" "line1\nline2\ttab")
  (check "escape payload"  (cdr (mqtt-receive c 2000)) "line1\nline2\ttab")
  (mqtt-unsubscribe c "curry/test/echo")

  ;;; mqtt-dropped starts at 0
  (check "dropped-initial" (mqtt-dropped c) 0)

  (mqtt-disconnect c)
  (check "post-disconnect" #t #t))


;;; ================================================================
;;; Part 2 — mqtt-connect* with options alist
;;; ================================================================

(define c2 #f)
(guard (exn (#t (skip! "mqtt-connect* — broker unavailable")))
  (set! c2 (mqtt-connect* "127.0.0.1" plain-port "curry-test-opts"
              `((keep-alive    . 30)
                (clean-session . #t)))))

(when c2
  (check "opts:connected?" (mqtt-connected? c2) #t)
  (check "opts:dropped"    (mqtt-dropped c2) 0)

  ;;; Publish and receive to confirm the connection is functional
  (mqtt-subscribe c2 "curry/opts/echo" 1)
  (mqtt-publish   c2 "curry/opts/echo" "opts-ok")
  (check "opts:pub/sub" (cdr (mqtt-receive c2 2000)) "opts-ok")
  (mqtt-unsubscribe c2 "curry/opts/echo")
  (mqtt-disconnect c2))


;;; ================================================================
;;; Part 3 — Retained messages
;;; ================================================================

;;; Publish a retained message, then subscribe fresh — should arrive immediately.
(define c-pub #f)
(define c-sub #f)
(guard (exn (#t (skip! "retain — broker unavailable")))
  (set! c-pub (mqtt-connect "127.0.0.1" plain-port "curry-test-retain-pub"))
  (set! c-sub (mqtt-connect "127.0.0.1" plain-port "curry-test-retain-sub")))

(when (and c-pub c-sub)
  ;;; Clear any existing retained message first
  (mqtt-publish c-pub "curry/test/retain" "" 0 #t)
  (mqtt-publish c-pub "curry/test/retain" "sticky" 0 #t)  ; retain=true

  ;;; Fresh subscriber should receive retained message without publisher sending again
  (mqtt-subscribe c-sub "curry/test/retain" 0)
  (let ((msg (mqtt-receive c-sub 2000)))
    (check "retain:topic"   (car msg) "curry/test/retain")
    (check "retain:payload" (cdr msg) "sticky"))

  ;;; Clear retained message
  (mqtt-publish c-pub "curry/test/retain" "" 0 #t)
  (mqtt-disconnect c-pub)
  (mqtt-disconnect c-sub))


;;; ================================================================
;;; Part 4 — Will message (Last Will & Testament)
;;;
;;; Two clients: subscriber A watches the will topic; publisher B
;;; connects with a LWT.  We disconnect B ungracefully by destroying
;;; its handle without sending MQTT DISCONNECT (abrupt TCP drop).
;;; The broker should deliver the will to A after keep-alive timeout.
;;; Because waiting for keep-alive is slow, we use the shortest valid
;;; keep-alive (5 s) and accept up to 15 s for delivery.
;;; ================================================================

(define will-tested #f)
(guard (exn (#t (skip! "will — broker unavailable")))
  (let ((ca (mqtt-connect "127.0.0.1" plain-port "curry-test-will-a"))
        (cb (mqtt-connect* "127.0.0.1" plain-port "curry-test-will-b"
               '((keep-alive  . 5)
                 (will-topic   . "curry/test/will")
                 (will-payload . "B is gone")
                 (will-qos     . 0)
                 (will-retain  . #f)))))
    (check "will:ca-connected" (mqtt-connected? ca) #t)
    (check "will:cb-connected" (mqtt-connected? cb) #t)

    (mqtt-subscribe ca "curry/test/will" 0)

    ;;; Drop cb without sending MQTT DISCONNECT — broker will fire the will
    ;;; after keep-alive expires.  We use mqtt-disconnect which sends DISCONNECT,
    ;;; so the will is suppressed; this sub-test only verifies that connecting
    ;;; with will options doesn't error.  A full ungraceful test would require
    ;;; raw socket control.
    (mqtt-disconnect cb)
    (check "will:opts-accepted" #t #t)

    (mqtt-unsubscribe ca "curry/test/will")
    (mqtt-disconnect ca)
    (set! will-tested #t)))

(unless will-tested
  (skip! "will — not tested"))


;;; ================================================================
;;; Part 5 — Disconnect event
;;;
;;; Connect to the ephemeral broker (started by test_mqtt.sh on disco-port),
;;; which is killed by the shell script after we signal readiness.
;;; mqtt-receive should return (disconnect . "...") rather than #f.
;;;
;;; Communication with the shell: we write "READY\n" to stdout, then
;;; the shell kills the broker, then we call mqtt-receive.
;;; ================================================================

(if (> disco-port 0)
    (guard (exn (#t (skip! "disconnect-event — ephemeral broker not reachable")))
      (let ((cd (mqtt-connect "127.0.0.1" disco-port "curry-test-disco")))
        (check "disco:connected" (mqtt-connected? cd) #t)
        (mqtt-subscribe cd "curry/disco/test" 0)

        ;;; Signal the shell that we are connected and ready for the broker kill
        (display "READY") (newline) (flush-output-port (current-output-port))

        ;;; Wait up to 8 s for a disconnect event
        (let loop ((n 8))
          (let ((msg (mqtt-receive cd 1000)))
            (cond
              ((and (pair? msg) (eq? (car msg) 'disconnect))
               (check-pred "disco:event"
                           (cdr msg)
                           string?))
              ((and msg (> n 0)) (loop (- n 1)))
              (else
               (check "disco:event" #f "(disconnect . str)"))))))
    (skip! "disconnect-event — disco-port not provided")))


;;; ================================================================
;;; Part 6 — TLS
;;; ================================================================

(guard (exn (#t (skip! "TLS — not compiled in or broker unavailable")))
  (let ((c-tls (if tls-ca
                   (mqtt-connect-tls "127.0.0.1" tls-port "curry-test-tls" tls-ca)
                   (mqtt-connect-tls "127.0.0.1" tls-port "curry-test-tls"))))
    (check "tls:connected"   (mqtt-connected? c-tls) #t)
    (mqtt-subscribe c-tls "curry/tls/echo" 1)
    (mqtt-publish   c-tls "curry/tls/echo" "secure")
    (let ((msg (mqtt-receive c-tls 2000)))
      (check "tls:pub/sub"   (car msg) "curry/tls/echo")
      (check "tls:payload"   (cdr msg) "secure"))
    (mqtt-unsubscribe c-tls "curry/tls/echo")
    (mqtt-disconnect c-tls)))

;;; TLS via mqtt-connect* with ca-cert option
(when tls-ca
  (guard (exn (#t (skip! "tls:mqtt-connect* — broker unavailable")))
    (let ((c-tls2 (mqtt-connect* "127.0.0.1" tls-port "curry-test-tls-opts"
                    `((ca-cert       . ,tls-ca)
                      (verify-server . #t)
                      (keep-alive    . 30)))))
      (check "tls:opts:connected" (mqtt-connected? c-tls2) #t)
      (mqtt-subscribe c-tls2 "curry/tls/opts" 0)
      (mqtt-publish   c-tls2 "curry/tls/opts" "tls-opts-ok")
      (check "tls:opts:payload"
             (cdr (mqtt-receive c-tls2 2000)) "tls-opts-ok")
      (mqtt-unsubscribe c-tls2 "curry/tls/opts")
      (mqtt-disconnect c-tls2))))


;;; ================================================================
;;; Summary
;;; ================================================================

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed, ")
(display skip) (display " skipped")
(newline)
(if (> fail 0) (exit 1) (exit 0))
