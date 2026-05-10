# Module: (curry mqtt)

MQTT client built on the [Eclipse Paho C](https://github.com/eclipse/paho.mqtt.c) synchronous API (`MQTTClient`). Supports MQTT 3.1.1, QoS 0/1/2, wildcard subscriptions, and TLS when linked against `libpaho-mqtt3cs`.

## Installation

```bash
# macOS
brew install libpaho-mqtt

# Debian / Ubuntu
sudo apt install libpaho-mqtt-dev

# Start a broker locally (Mosquitto)
brew install mosquitto && mosquitto   # macOS
sudo apt install mosquitto && mosquitto  # Linux
```

Enable: `-DBUILD_MODULE_MQTT=ON` (default on). CMake detects `libpaho-mqtt3cs` (TLS) automatically and falls back to `libpaho-mqtt3c` (plain only) if the SSL-capable library is absent.

## Import

```scheme
(import (curry mqtt))
```

## Connection

```scheme
; Plain TCP
(mqtt-connect host port client-id)                    ; → client
(mqtt-connect host port client-id username password)  ; with credentials

; TLS  (requires libpaho-mqtt3cs at build time)
(mqtt-connect-tls host port client-id)                ; system CA bundle
(mqtt-connect-tls host port client-id ca-cert)        ; custom CA cert path
(mqtt-connect-tls host port client-id ca-cert user password)

(mqtt-disconnect  client)   ; → void  (flushes pending queue)
(mqtt-connected?  client)   ; → bool
```

`client-id` must be unique per broker connection. The connection uses a clean session (`cleansession=1`) with a 60-second keep-alive.

## Publish

```scheme
(mqtt-publish client topic payload)              ; QoS 0, no retain
(mqtt-publish client topic payload qos)          ; QoS 0, 1, or 2
(mqtt-publish client topic payload qos retain?)  ; retain flag
```

- `payload` is a string.
- QoS 1 and 2 block until the broker acknowledges (up to 5 seconds).
- `retain?` is any truthy value; `#f` or omitted means no retain.

## Subscribe / Unsubscribe

```scheme
(mqtt-subscribe   client topic)        ; QoS 1 default
(mqtt-subscribe   client topic qos)    ; QoS 0, 1, or 2
(mqtt-unsubscribe client topic)
```

MQTT wildcard characters work normally: `+` matches one level, `#` matches the rest of the path.

## Receive

```scheme
(mqtt-receive client)             ; → (topic . payload) | #f  (5s timeout)
(mqtt-receive client timeout-ms)  ; custom timeout in milliseconds
```

Blocks up to `timeout-ms` milliseconds waiting for the next message on any active subscription. Returns `(topic . payload)` where both are strings, or `#f` if no message arrived within the timeout.

Incoming messages are buffered in a 256-entry native ring buffer. When the buffer is full, new messages are dropped silently. The Paho callback thread never touches the Scheme heap.

## Examples

### Basic pub/sub

```scheme
(import (curry mqtt))

(define c (mqtt-connect "localhost" 1883 "my-client"))

(mqtt-subscribe c "sensors/#" 1)

(mqtt-publish c "sensors/temp" "23.4")

(let ((msg (mqtt-receive c 2000)))
  (when msg
    (display (car msg)) (display " → ") (display (cdr msg)) (newline)))
; sensors/temp → 23.4

(mqtt-disconnect c)
```

### Fan-out to multiple topics

```scheme
(import (curry mqtt))

(define c (mqtt-connect "localhost" 1883 "publisher"))

(for-each
  (lambda (reading)
    (mqtt-publish c (string-append "home/" (car reading)) (cdr reading) 1))
  '(("kitchen/temp" . "21.1")
    ("bedroom/temp" . "19.8")
    ("living/temp"  . "22.3")))

(mqtt-disconnect c)
```

### Receive loop with actors

```scheme
(import (curry mqtt))

; Subscriber actor — forwards messages to a handler actor
(define (make-subscriber broker-host broker-port topic handler)
  (spawn (lambda ()
    (define c (mqtt-connect broker-host broker-port
                            (string-append "sub-" (number->string (random 99999)))))
    (mqtt-subscribe c topic 1)
    (let loop ()
      (let ((msg (mqtt-receive c 5000)))
        (when msg (send! handler msg)))
      (loop)))))

(define printer
  (spawn (lambda ()
    (let loop ()
      (let ((msg (receive)))
        (display (car msg)) (display ": ") (display (cdr msg)) (newline))
      (loop)))))

(define sub (make-subscriber "localhost" 1883 "alerts/#" printer))
```

### TLS connection with a custom CA

```scheme
(import (curry mqtt))

(define c (mqtt-connect-tls "broker.example.com" 8883
                             "secure-client"
                             "/etc/ssl/certs/broker-ca.crt"
                             "alice" "s3cr3t"))

(mqtt-subscribe c "private/data" 1)
(let ((msg (mqtt-receive c 3000)))
  (display (cdr msg)) (newline))

(mqtt-disconnect c)
```

### Retained configuration topic

```scheme
(import (curry mqtt))

(define c (mqtt-connect "localhost" 1883 "config-publisher"))

; Publish with retain=1 so late subscribers get the last value immediately
(mqtt-publish c "config/max-temp" "25.0" 1 #t)

(mqtt-disconnect c)
```

## Notes

- **One connection per thread.** The `MQTTClient` handle is not thread-safe; use one connection per actor.
- **Buffer overflow.** The receive queue holds 256 messages. A slow consumer with a fast publisher will lose messages. Increase `MQTT_QCAP` in `mqtt.c` and recompile if needed.
- **QoS 0 delivery.** Fire-and-forget; no acknowledgement. Messages may be lost on an unreliable network.
- **QoS 1 delivery.** At-least-once; acknowledged by the broker. `mqtt-publish` blocks until the `PUBACK` arrives (up to 5 s).
- **Clean session.** Each connection starts fresh — no persistent subscriptions or queued messages are restored from the broker.
- **Payload encoding.** Payloads are treated as UTF-8 strings. Binary payloads that contain null bytes will be truncated at the first `\0`; use `mqtt-publish` with a string-encoded representation (base64, hex, JSON) for binary data.
- **TLS.** `mqtt-connect-tls` without a `ca-cert` argument uses the system CA bundle. Peer certificate verification is always enabled; self-signed certs require an explicit `ca-cert` path.
