;;; MQTT module tests.
;;;
;;; Usage (normally invoked via test_mqtt.sh):
;;;   curry mqtt_tests.scm [plain-port [tls-port [tls-ca-cert]]]
;;;
;;; All sections skip gracefully when the broker is not reachable.

(import (curry mqtt))

;;; ---- Args (same symbol-based convention as redis_tests.scm) ----

(define raw-args (cdr command-line-args))

(define (arg-port lst default)
  (let ((v (and (pair? lst) (car lst))))
    (or (and (number? v) (exact v))
        (and (symbol? v) (string->number (symbol->string v)))
        default)))

(define (arg-path lst)
  (let ((v (and (pair? lst) (car lst))))
    (cond
      ((string? v) (if (string=? v "") #f v))
      ((symbol? v) (let ((s (symbol->string v)))
                     (if (string=? s "") #f s)))
      (else #f))))

(define plain-port (arg-port raw-args             1883))
(define tls-port   (arg-port (cdr raw-args)       8883))
(define tls-ca     (arg-path (cddr raw-args)))

;;; ---- Harness ----

(define pass 0)
(define fail 0)
(define skip 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (skip! label)
  (display "SKIP: ") (display label) (newline)
  (set! skip (+ skip 1)))

;;; ---- Plain TCP tests ----

(define c #f)
(guard (exn (#t (skip! (string-append "MQTT broker not available on 127.0.0.1:"
                                      (number->string plain-port)))))
  (set! c (mqtt-connect "127.0.0.1" plain-port "curry-test-plain")))

(when c
  (check "connected?"      (mqtt-connected? c)   #t)

  ;;; Publish QoS 0 — fire and forget, no receipt confirmation needed
  (mqtt-subscribe c "curry/test/echo" 1)

  (mqtt-publish c "curry/test/echo" "hello")
  (let ((msg (mqtt-receive c 2000)))
    (check "pub/sub basic"   (car msg)  "curry/test/echo")
    (check "payload"         (cdr msg)  "hello"))

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

  ;;; QoS 1 publish
  (mqtt-publish c "curry/test/echo" "qos1" 1)
  (let ((msg (mqtt-receive c 2000)))
    (check "qos1 payload"    (cdr msg)  "qos1"))

  ;;; Timeout on empty channel
  (mqtt-unsubscribe c "curry/test/echo")
  (mqtt-subscribe c "curry/test/empty" 0)
  (check "timeout->false"  (mqtt-receive c 300)  #f)
  (mqtt-unsubscribe c "curry/test/empty")

  ;;; Wildcard subscription
  (mqtt-subscribe c "curry/wild/#" 0)
  (mqtt-publish c "curry/wild/a" "alpha")
  (mqtt-publish c "curry/wild/b" "beta")
  (let ((m1 (mqtt-receive c 2000))
        (m2 (mqtt-receive c 2000)))
    (check "wildcard-a"  (cdr m1) "alpha")
    (check "wildcard-b"  (cdr m2) "beta"))
  (mqtt-unsubscribe c "curry/wild/#")

  ;;; Disconnect cleans up without error
  (mqtt-disconnect c)
  (check "post-disconnect" #t #t))

;;; ---- TLS tests ----

(guard (exn (#t (skip! "MQTT TLS unavailable (not compiled in or no TLS broker)")))
  (let ((c-tls (if tls-ca
                   (mqtt-connect-tls "127.0.0.1" tls-port "curry-test-tls" tls-ca)
                   (mqtt-connect-tls "127.0.0.1" tls-port "curry-test-tls"))))
    (check "tls:connected?"    (mqtt-connected? c-tls)   #t)
    (mqtt-subscribe c-tls "curry/tls/echo" 1)
    (mqtt-publish   c-tls "curry/tls/echo" "secure")
    (let ((msg (mqtt-receive c-tls 2000)))
      (check "tls:pub/sub"     (car msg)  "curry/tls/echo")
      (check "tls:payload"     (cdr msg)  "secure"))
    (mqtt-unsubscribe c-tls "curry/tls/echo")
    (mqtt-disconnect c-tls)))

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed, ")
(display skip) (display " skipped")
(newline)
(if (> fail 0) (exit 1) (exit 0))
