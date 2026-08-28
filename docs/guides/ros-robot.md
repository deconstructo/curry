# Talking to a tracked robot over rosbridge

*unreleased*

A worked example tying together [`(curry ros)`](../reference/module-ros.md) and [`(curry rpi)`](../reference/module-rpi.md): a Raspberry Pi drives a small tracked/differential-drive robot's motors directly over GPIO/PWM, while ROS (running on the same Pi, or on a separate machine on the same network) provides teleop, sensor topics, and everything else in the ROS ecosystem — `teleop_twist_keyboard`, `rviz`, SLAM packages, whatever you want to point at it — without curry needing to speak DDS or link against any ROS client library.

This assumes the kind of kit a lot of hobby tracked-robot chassis ship as: two DC gearmotors (one per side), each driven through an H-bridge (e.g. an L298N or similar) with a direction pin pair and a PWM speed-enable pin. If your chassis is currently wired to an Arduino, the GPIO/PWM side of this guide is exactly the part that changes when you move the motor driver's control lines from the Arduino's pins to the Pi's — the H-bridge and motors themselves don't care which microcontroller is driving them.

## Architecture

```
teleop_twist_keyboard  --/cmd_vel-->  rosbridge_server  <--ws://-->  curry (on the Pi)
   (or rviz, or a                         (JSON over                    |
    Nav2 planner, ...)                     WebSocket)                   v
                                                                  (curry rpi) GPIO/PWM
                                                                    -> H-bridge -> motors
```

curry subscribes to `/cmd_vel` (a standard `geometry_msgs/Twist`: linear and angular velocity) and converts it into left/right wheel speeds, the same differential-drive mixing every two-wheeled ROS robot's motor driver node does. Nothing here is robot-specific to curry — this is the same job `ros_control`/a vendor's own motor driver node does, just written directly against `(curry rpi)`.

## 1. Wiring (adjust pin numbers for your own H-bridge/chassis)

| Signal | Pi GPIO (BCM) |
|--------|---------------|
| Left motor direction A | 17 |
| Left motor direction B | 27 |
| Left motor PWM enable | PWM chip 0, channel 0 |
| Right motor direction A | 22 |
| Right motor direction B | 23 |
| Right motor PWM enable | PWM chip 0, channel 1 |

See [Raspberry Pi guide](RPI.md) for enabling the PWM overlay and hardware groups first.

## 2. The motor driver piece

```scheme
(import (curry rpi))

(define left-a  (gpio-open 0 17 'output))
(define left-b  (gpio-open 0 27 'output))
(define left-pwm (pwm-open 0 0))

(define right-a (gpio-open 0 22 'output))
(define right-b (gpio-open 0 23 'output))
(define right-pwm (pwm-open 0 1))

(pwm-enable! left-pwm)
(pwm-enable! right-pwm)

;; speed: -1.0 .. 1.0 (negative = reverse)
(define (%set-motor! a b pwm speed)
  (let ((forward? (>= speed 0))
        (duty (inexact->exact (round (* (abs (max -1.0 (min 1.0 speed))) 20000000)))))
    (gpio-write a (if forward? 1 0))
    (gpio-write b (if forward? 0 1))
    (pwm-set! pwm 20000000 duty)))

(define (set-left-speed! speed)  (%set-motor! left-a left-b left-pwm speed))
(define (set-right-speed! speed) (%set-motor! right-a right-b right-pwm speed))

(define (stop!) (set-left-speed! 0) (set-right-speed! 0))
```

## 3. Differential-drive mixing from a Twist message

A `geometry_msgs/Twist` has `linear.x` (forwards/backwards, m/s) and `angular.z` (turn rate, rad/s). Standard mixing, scaled to curry's `-1.0..1.0` motor range by whatever your robot's max speed actually is (`max-linear`/`max-angular` below are placeholders — measure your own chassis):

```scheme
(define max-linear 0.3)   ; m/s at full stick
(define max-angular 2.0)  ; rad/s at full stick
(define track-width 0.15) ; metres between the two tracks -- measure yours

(define (%field alist name) (let ((p (assoc name alist))) (if p (cdr p) 0)))

(define (twist->wheel-speeds twist)
  (let* ((linear  (%field (%field twist "linear")  "x") )
         (angular (%field (%field twist "angular") "z"))
         (v (/ linear max-linear))
         (w (/ (* angular (/ track-width 2)) max-angular)))
    (values (- v w) (+ v w))))
```

## 4. Wiring it to ROS

```scheme
(import (curry ros) (curry rpi))

(define conn (ros-connect "ws://localhost:9090/"))

(ros-subscribe! conn "/cmd_vel"
  (lambda (twist)
    (call-with-values
     (lambda () (twist->wheel-speeds twist))
     (lambda (left right) (set-left-speed! left) (set-right-speed! right)))))
```

That's a working teleop robot: run `rosbridge_server` and `teleop_twist_keyboard` (or drive `/cmd_vel` from anything else in the ROS ecosystem — a joystick node, a Nav2 planner, `rviz`'s own teleop panel), and curry turns the incoming `Twist` messages into motor movement on the Pi.

Two things worth building in before actually running this on hardware:

- **A watchdog.** If the WebSocket connection drops mid-drive, the motors should stop, not keep running the last command forever. `ros-connected?` plus a periodic check (or watching for `ws-recv!` returning an eof-object, which `(curry ros)`'s reader actor already treats as "connection closed") is the hook to call `stop!` from.
- **A speed cap independent of what `/cmd_vel` says**, at least while testing — clamp `left`/`right` to something safely slow until you trust the wiring and the mixing math.

## 5. Publishing something back

Real ROS setups expect odometry (`/odom`) and usually a diagnostics topic. A minimal heartbeat, so at least `rostopic echo`/`ros2 topic echo` on another machine shows the robot is alive:

```scheme
(ros-advertise! conn "/curry_robot/heartbeat" "std_msgs/String")

(spawn (lambda ()
  (let loop ()
    (ros-publish! conn "/curry_robot/heartbeat" (list (cons "data" "alive")))
    (thread-sleep! 1)
    (loop))))
```

(`thread-sleep!` here is [SRFI-18](../reference/srfi/s18.md)'s, imported via `(import (srfi s18 multithreading))`.)

## See also

- [`(curry ros)`](../reference/module-ros.md) — full API reference
- [`(curry rpi)`](../reference/module-rpi.md) — the GPIO/I2C/SPI/PWM module used above
- [Raspberry Pi guide](RPI.md) — building curry with the rpi module, wiring basics, troubleshooting
