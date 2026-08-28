# Module: (curry ros)

*unreleased*

A client for the [rosbridge v2.0 JSON protocol](https://github.com/RobotWebTools/rosbridge_suite/blob/ros1/ROSBRIDGE_PROTOCOL.md), letting curry talk to a running [ROS1 or ROS2](https://www.ros.org/) system through `rosbridge_server` — topics (publish/subscribe) and services (call/advertise) — without any ROS client library, DDS transport, or ROS install linked into curry itself. Built entirely on [`(curry websocket)`](module-websocket.md) + `(curry json)` + `(curry sync)` + curry's actor system.

This is the client side only: it talks to a `rosbridge_server` instance (part of `rosbridge_suite`, run on the robot or on any machine with access to the ROS graph). curry does not join the ROS graph directly — no node discovery, no DDS/TCPROS wire protocol. For that, a native `rcl`/`rclc` FFI binding or a from-scratch DDS-RTPS implementation would be a separate, much larger module.

## Installation

No extra packages required on the curry side — pure Scheme, no CMake flag. You do need a reachable `rosbridge_server`:

```bash
# ROS1
sudo apt install ros-<distro>-rosbridge-server
roslaunch rosbridge_server rosbridge_websocket.launch

# ROS2
sudo apt install ros-<distro>-rosbridge-suite
ros2 launch rosbridge_server rosbridge_websocket_launch.xml
```

Default port is 9090.

## Import

```scheme
(import (curry ros))
```

## Connecting

### `(ros-connect url)` → conn

Connects to a `rosbridge_server` WebSocket endpoint (e.g. `"ws://localhost:9090/"`) and spawns a background actor ("the reader") that owns the connection's read side for the rest of its life: it decodes every incoming frame as JSON and dispatches it — topic messages go to that topic's subscribers, service responses wake up the matching blocked `ros-call-service`, and incoming service *calls* (for a service curry itself advertised) get handed to the registered handler.

### `(ros-close! conn)`

### `(ros-connected? conn)` → boolean

## Topics

### `(ros-advertise! conn topic type)`

Declares that curry will publish on `topic` with ROS message type `type` (e.g. `"std_msgs/String"`, `"geometry_msgs/Twist"`).

### `(ros-unadvertise! conn topic)`

### `(ros-publish! conn topic msg)`

Publishes `msg` on `topic`. `msg` is an ordinary curry JSON value: an association list `((field . value) ...)` for a message with fields, matching the ROS message's own field names.

```scheme
(ros-advertise! conn "/cmd_vel" "geometry_msgs/Twist")
(ros-publish! conn "/cmd_vel"
  (list (cons "linear"  (list (cons "x" 0.2) (cons "y" 0) (cons "z" 0)))
        (cons "angular" (list (cons "x" 0) (cons "y" 0) (cons "z" 0.0)))))
```

### `(ros-subscribe! conn topic callback [type])`

Registers `callback` (a one-argument procedure) to be invoked with the decoded message every time one arrives on `topic`. Multiple `ros-subscribe!` calls on the same topic each add an independent callback (all of them fire); only the first actually asks the server to subscribe.

**`callback` runs on the reader actor** — the same one decoding every other incoming message on this connection. Keep it fast (no blocking I/O, no long computation); if you need to do real work in response, `send!` the message on to a separate actor you've already spawned and return immediately.

```scheme
(define scan-actor (spawn (lambda () (let loop () (receive) (loop)))))
(ros-subscribe! conn "/scan" (lambda (msg) (send! scan-actor msg)))
```

### `(ros-unsubscribe! conn topic)`

Removes *all* callbacks registered on `topic` and asks the server to unsubscribe.

## Services

### `(ros-call-service conn service args [timeout])` → (values ok? values)

Calls `service` with `args` (a list or vector of JSON values, positional per the service's request fields) and **blocks the calling thread** until the response arrives, or `timeout` seconds elapse if given. Returns two values: whether the call succeeded (`result` in the rosbridge protocol) and the response values. Safe to call concurrently from several actors — each call gets its own id and is matched independently.

Decoded JSON arrays come back as vectors (matching `(curry json)`'s own `json-parse` convention throughout curry), even though `args` going out may be given as a plain list.

```scheme
(call-with-values
 (lambda () (ros-call-service conn "/add_two_ints" (list 2 3)))
 (lambda (ok? values) (display values)))   ; => #(5)
```

### `(ros-advertise-service! conn service type handler)`

Advertises that curry will serve `service` (ROS service type `type`). `handler` is a one-argument procedure receiving the call's `args`, and must return **two values** via `(values ok? result-values)` — `ok?` is the boolean the caller sees as `result`, `result-values` is a list or vector of response values.

```scheme
(ros-advertise-service! conn "/curry/ping" "std_srvs/Trigger"
  (lambda (args) (values #t (list "pong"))))
```

### `(ros-unadvertise-service! conn service)`

## Thread safety

All of a connection's mutable bookkeeping (subscriptions, pending service calls, advertised services, the id counter) is protected by one mutex per connection, since the reader actor and any number of publisher/caller actors touch it concurrently. See [`(curry websocket)`](module-websocket.md#thread-safety) for the underlying frame-write serialization this relies on.

## See also

- [`(curry websocket)`](module-websocket.md) — the transport this module is built on
- [Talking to a tracked robot over rosbridge](../guides/ros-robot.md) — a worked example: teleop over `/cmd_vel`, reading `/scan`, tying into `(curry rpi)` for direct motor control
- [`(curry rpi)`](module-rpi.md) — GPIO/I2C/SPI/PWM, for driving motors directly instead of (or alongside) ROS
