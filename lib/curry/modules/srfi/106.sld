(define-library (srfi 106)
  (import (srfi s106 sockets))
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
    *shut-rd* *shut-wr* *shut-rdwr*))
