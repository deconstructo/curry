;;; (srfi s106 sockets) -- SRFI 106: Basic Socket Interface
;;;
;;; The native primitives (make-client-socket, make-server-socket, socket?,
;;; socket-accept, socket-send, socket-recv, socket-shutdown, socket-close,
;;; socket-input-port, socket-output-port) and the plain fixnum constants
;;; live in modules/network/srfi106.c, registered directly under these
;;; exact SRFI-106 names -- same convention (curry posix)/SRFI 170 already
;;; uses (see lib/curry/modules/srfi/s170/posix.scm): the C module defines
;;; the spec's own procedure names directly, this file just re-exports
;;; them plus adds the pieces that only make sense as Scheme: call-with-
;;; socket (a thin dynamic-wind wrapper) and the six name->constant macros
;;; the spec itself defines (address-family, address-info, socket-domain,
;;; ip-protocol, message-type, shutdown-method) plus socket-merge-flags/
;;; socket-purge-flags.
;;;
;;; The six macros are pure compile-time symbol->constant lookups with no
;;; runtime behavior -- syntax-rules with literal identifiers, not a C
;;; primitive (curry's C module API has no macro-registration story).
;;; address-family/socket-domain/ip-protocol take exactly one name each
;;; (the underlying platform value is a single selector, never combined);
;;; address-info/message-type/shutdown-method take one or more names and
;;; combine them -- see shutdown-method's own comment below for why that
;;; combination is NOT simply bitwise-or for every category.

(define-library (srfi s106 sockets)
  (import (curry network) (scheme base))
  (export
    make-client-socket make-server-socket socket? socket-accept
    socket-send socket-recv socket-shutdown socket-close
    socket-input-port socket-output-port call-with-socket
    address-family address-info socket-domain ip-protocol
    message-type shutdown-method socket-merge-flags socket-purge-flags
    *af-unspec* *af-inet* *af-inet6*
    *sock-stream* *sock-dgram*
    *ai-canonname* *ai-numerichost* *ai-v4mapped* *ai-all* *ai-addrconfig*
    *ipproto-ip* *ipproto-tcp* *ipproto-udp*
    *msg-peek* *msg-oob* *msg-waitall*
    *shut-rd* *shut-wr* *shut-rdwr*)
  (begin

    ;; Closes `socket` after `proc` returns OR raises -- dynamic-wind's
    ;; after-thunk runs on both paths, matching call-with-port's own
    ;; close-on-either-path contract (prim_call_with_port, builtins.c).
    (define (call-with-socket socket proc)
      (dynamic-wind
        (lambda () #f)
        (lambda () (proc socket))
        (lambda () (socket-close socket))))

    (define-syntax address-family
      (syntax-rules (inet inet6 unspec)
        ((_ inet)   *af-inet*)
        ((_ inet6)  *af-inet6*)
        ((_ unspec) *af-unspec*)))

    (define-syntax socket-domain
      (syntax-rules (stream datagram)
        ((_ stream)   *sock-stream*)
        ((_ datagram) *sock-dgram*)))

    (define-syntax ip-protocol
      (syntax-rules (ip tcp udp)
        ((_ ip)  *ipproto-ip*)
        ((_ tcp) *ipproto-tcp*)
        ((_ udp) *ipproto-udp*)))

    ;; address-info names are genuinely independent getaddrinfo(3) AI_*
    ;; bits on every platform -- safe to combine with plain bitwise-or.
    (define-syntax %address-info-1
      (syntax-rules (canonname numerichost v4mapped all addrconfig)
        ((_ canonname)   *ai-canonname*)
        ((_ numerichost) *ai-numerichost*)
        ((_ v4mapped)    *ai-v4mapped*)
        ((_ all)         *ai-all*)
        ((_ addrconfig)  *ai-addrconfig*)))
    (define-syntax address-info
      (syntax-rules ()
        ((_ name)          (%address-info-1 name))
        ((_ name name* ...) (bitwise-or (%address-info-1 name)
                                         (address-info name* ...)))))

    ;; message-type names are genuinely independent MSG_* bits too.
    ;; `none` (0) is SRFI 106's own explicit "no flags" name -- there is
    ;; no MSG_NONE in POSIX, 0 already means exactly that to send/recv.
    (define-syntax %message-type-1
      (syntax-rules (none peek oob wait-all)
        ((_ none)     0)
        ((_ peek)     *msg-peek*)
        ((_ oob)      *msg-oob*)
        ((_ wait-all) *msg-waitall*)))
    (define-syntax message-type
      (syntax-rules ()
        ((_ name)          (%message-type-1 name))
        ((_ name name* ...) (bitwise-or (%message-type-1 name)
                                         (message-type name* ...)))))

    ;; shutdown-method is the one category that is NOT plain bitwise-or
    ;; over real platform values -- see *shut-rd*/*shut-wr*/*shut-rdwr*'s
    ;; own comment in srfi106.c (fn_socket_shutdown) for the full
    ;; reasoning: raw POSIX SHUT_RD/SHUT_WR/SHUT_RDWR (0/1/2 on every
    ;; platform checked) are not independent bits, so combining them the
    ;; same way address-info/message-type do would silently produce the
    ;; wrong value ((shutdown-method read write) via SHUT_RD|SHUT_WR =
    ;; 0|1 = 1, indistinguishable from plain SHUT_WR). *shut-rd*/*shut-
    ;; wr*/*shut-rdwr* are instead a clean 2-bit encoding (1/2/3) that DOES
    ;; combine correctly with bitwise-or, translated back to the real
    ;; platform constant inside socket-shutdown's own C implementation.
    (define-syntax %shutdown-method-1
      (syntax-rules (read write)
        ((_ read)  *shut-rd*)
        ((_ write) *shut-wr*)))
    (define-syntax shutdown-method
      (syntax-rules ()
        ((_ name)          (%shutdown-method-1 name))
        ((_ name name* ...) (bitwise-or (%shutdown-method-1 name)
                                         (shutdown-method name* ...)))))

    (define (socket-merge-flags . flags)
      (apply bitwise-or 0 flags))

    (define (socket-purge-flags base-flag . flags)
      (bitwise-and base-flag (bitwise-not (apply bitwise-or 0 flags))))))
