;;; char-ready? / u8-ready? / socket-ready? on file and socket ports (#366).
;;;
;;; char-ready? used to return #t on every file and socket port, so a loop
;;; that polled with it and then called read-char blocked forever on an idle
;;; connection. socket-ready? looked only at the file descriptor, so bytes the
;;; C library had already read into its stream buffer were reported as "not
;;; ready". Self-contained: loopback sockets in this process.

(import (curry network))
(import (scheme base))
(import (scheme file))
(import (scheme write))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;; A connected pair on loopback: (client-in client-out server-in server-out).
;; connect() succeeds against the listen backlog before accept() is called,
;; so one thread can do both.
(define (connect-pair listener)
  (let* ((c (tcp-connect "127.0.0.1" (socket-local-port listener)))
         (s (tcp-accept listener 5000)))
    (list (car c) (cdr c) (car s) (cdr s))))
(define (send! out str) (write-string str out) (flush-output-port out))

(define listener (tcp-listen 0))
(define pair (connect-pair listener))
(define cin (car pair)) (define cout (cadr pair))
(define sin (caddr pair)) (define sout (cadddr pair))

;;; 1. An idle connection is not ready ───────────────────────────────────────
(check "char-ready? is #f on an idle socket" (char-ready? sin) #f)
(check "u8-ready? is #f on an idle socket" (u8-ready? sin) #f)
(check "socket-ready? is #f on an idle socket" (socket-ready? sin 0) #f)

;;; 2. Data arrives ──────────────────────────────────────────────────────────
(send! cout "ab\n")
(check "socket-ready? sees the data" (socket-ready? sin 2000) #t)
(check "char-ready? is #t once data has arrived" (char-ready? sin) #t)
(check "read-char" (read-char sin) #\a)

;; The byte b is now in the stream buffer, not readable on the descriptor.
(check "char-ready? is #t for a buffered byte" (char-ready? sin) #t)
(check "socket-ready? is #t for a buffered byte (was #f)" (socket-ready? sin 0) #t)
(check "read-char of the buffered byte" (read-char sin) #\b)
(check "read-char of the newline" (read-char sin) #\newline)

;;; 3. Drained again ─────────────────────────────────────────────────────────
(check "char-ready? is #f again once everything is read" (char-ready? sin) #f)
(check "socket-ready? is #f again" (socket-ready? sin 0) #f)
(check "socket-ready? on the connection's output port polls the descriptor, as before"
       (socket-ready? sout 0) #f)

;;; 4. peek-char ─────────────────────────────────────────────────────────────
(send! cout "x")
(socket-ready? sin 2000)              ; wait for it: loopback is fast, not instant
(check "peek-char" (peek-char sin) #\x)
(check "char-ready? is #t after a peek" (char-ready? sin) #t)
(check "socket-ready? is #t after a peek (the peeked character)" (socket-ready? sin 0) #t)
(check "read-char after the peek" (read-char sin) #\x)
(check "char-ready? is #f after it" (char-ready? sin) #f)

;;; 5. A multi-byte character sent in one write ──────────────────────────────
(send! cout "é")
(socket-ready? sin 2000)
(check "char-ready? with a two-byte character" (char-ready? sin) #t)
(check "read-char decodes it" (read-char sin) #\é)

;;; 6. The polling loop from the issue: one idle client must not block ──────
(define pair2 (connect-pair listener))
(define sin2 (caddr pair2))
(define cout2 (cadr pair2))
(send! cout2 "hi\n")
(socket-ready? sin2 2000)
(define (poll-all ports)
  ;; the first port that has a character, without ever blocking on one
  (let loop ((ps ports))
    (cond ((null? ps) #f)
          ((char-ready? (car ps)) (car ps))
          (else (loop (cdr ps))))))
(check "the loop skips the idle connection and finds the one with data"
       (eq? (poll-all (list sin sin2)) sin2) #t)
(check "and reads from it" (read-line sin2) "hi")
(check "nothing is ready now" (poll-all (list sin sin2)) #f)

;;; 7. End of file counts as ready: a read returns at once ───────────────────
;; tcp-connect's two ports share the socket; the peer sees the end of the
;; file only when both are closed.
(close-port cout)
(close-port cin)
(close-port sout)
(check "socket-ready? is #t when the peer has closed" (socket-ready? sin 2000) #t)
(check "char-ready? is #t at the end of the file" (char-ready? sin) #t)
(check "and read-char returns the eof object" (eof-object? (read-char sin)) #t)

;;; 8. Plain files and string ports ──────────────────────────────────────────
(define path "char_ready_test_file.tmp")
(when (file-exists? path) (delete-file path))     ; left behind by an aborted run
(call-with-output-file path (lambda (o) (write-string "abc" o)))
(define fin (open-input-file path))
(check "char-ready? is #t on a regular file" (char-ready? fin) #t)
(read-char fin) (read-char fin) (read-char fin)
(check "char-ready? is #t at the end of a regular file" (char-ready? fin) #t)
(close-port fin)
(delete-file path)

(define sp (open-input-string "z"))
(check "char-ready? on a string port with input" (char-ready? sp) #t)
(read-char sp)
(check "char-ready? on an exhausted string port is unchanged" (char-ready? sp) #f)

;;; Summary ──────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
