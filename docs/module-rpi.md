# RPi hardware module — `(curry rpi)`

GPIO, I2C, SPI, and PWM access for Raspberry Pi and other Linux embedded boards.

**Linux only.** Not supported on macOS or other non-Linux platforms.

## Dependencies

```bash
sudo apt install libgpiod-dev
```

I2C and SPI use kernel ioctls directly — no extra library needed beyond enabling
the interfaces in `raspi-config` (or equivalent).

## Building

```bash
cmake -B build -DBUILD_MODULE_RPI=ON
cmake --build build
```

## Quick start

```scheme
(import (curry rpi))

; Blink an LED on BCM pin 17
(define led (gpio-open 0 17 'output))
(gpio-write led 1)
(sleep 1)
(gpio-write led 0)
(gpio-close led)
```

## GPIO

Uses `libgpiod` — the modern kernel character-device interface. Requires the
user to be in the `gpio` group (or run as root).

```bash
sudo usermod -aG gpio $USER
```

### `(gpio-open chip line direction)` → gpio-handle

Open a GPIO line. `chip` is the chip number (usually `0` for
`/dev/gpiochip0`). `line` is the BCM pin number. `direction` is `'input` or
`'output`.

```scheme
(define led (gpio-open 0 17 'output))
(define btn (gpio-open 0 27 'input))
```

### `(gpio-read handle)` → 0 or 1

Read the current logic level.

```scheme
(display (gpio-read btn))   ; => 0 or 1
```

### `(gpio-write handle value)` → void

Set the output level. `value` must be `0` or `1`.

```scheme
(gpio-write led 1)   ; high
(gpio-write led 0)   ; low
```

### `(gpio-close handle)` → void

Release the line and close the chip. Always call this when done.

### `(gpio? v)` → bool

Return `#t` if `v` is a gpio handle.

## I2C

Uses `ioctl` on `/dev/i2c-N`. Enable I2C in `raspi-config` → Interfaces → I2C.
The user must be in the `i2c` group:

```bash
sudo usermod -aG i2c $USER
```

### `(i2c-open bus)` → i2c-handle

Open `/dev/i2c-N` where `N` is `bus` (usually `1` on modern Pi boards).

```scheme
(define bus (i2c-open 1))
```

### `(i2c-read handle addr reg nbytes)` → bytevector

Select device at `addr`, write register byte `reg`, then read `nbytes` bytes.
Returns a bytevector of length `nbytes`.

```scheme
; Read 2 bytes from register 0x00 of device at address 0x48
(define data (i2c-read bus #x48 #x00 2))
(display (bytevector-u8-ref data 0))
```

### `(i2c-write handle addr reg data)` → void

Select device at `addr`, write register byte `reg` followed by the bytes of
`data` (a bytevector).

```scheme
(i2c-write bus #x48 #x01 #u8(#x42 #x00))
```

### `(i2c-close handle)` → void

Close the file descriptor.

### `(i2c? v)` → bool

## SPI

Uses `ioctl` on `/dev/spidevN.M`. Enable SPI in `raspi-config` → Interfaces → SPI.

### `(spi-open bus device speed-hz mode)` → spi-handle

Open `/dev/spidevN.M`. `speed-hz` is the clock frequency in Hz. `mode` is
0–3 (SPI_MODE_0 through SPI_MODE_3).

```scheme
(define dev (spi-open 0 0 1000000 0))   ; 1 MHz, mode 0
```

### `(spi-transfer handle tx-bytevector)` → rx-bytevector

Full-duplex transfer. Sends `tx-bytevector` and returns a bytevector of the
same length containing the received bytes.

```scheme
(define rx (spi-transfer dev #u8(#xAB #x00 #x00)))
```

### `(spi-close handle)` → void

### `(spi? v)` → bool

## PWM

Uses the sysfs PWM interface at `/sys/class/pwm/`. Enable hardware PWM with
`dtoverlay=pwm` in `/boot/config.txt` (or `/boot/firmware/config.txt` on
Pi 5). The user must have write access to the sysfs nodes (usually requires
root, or a udev rule).

Periods and duty cycles are in **nanoseconds**.

### `(pwm-open chip channel)` → pwm-handle

Export and open PWM channel. `chip` is usually `0`; `channel` is `0` or `1`.

```scheme
(define pwm (pwm-open 0 0))
```

### `(pwm-set! handle period-ns duty-ns)` → void

Set the period and duty cycle in nanoseconds. Must be called before enabling.

```scheme
; Servo: 20ms period, 1.5ms duty (centre position)
(pwm-set! pwm 20000000 1500000)
```

### `(pwm-enable! handle)` → void

Start the PWM output.

### `(pwm-disable! handle)` → void

Stop the PWM output without closing.

### `(pwm-close handle)` → void

Disable, unexport, and free the handle.

### `(pwm? v)` → bool

## Example — MCP4725 DAC over I2C

```scheme
(import (curry rpi))

(define bus (i2c-open 1))

(define (dac-write! value)          ; value 0-4095
  (let* ((hi (arithmetic-shift value -8))
         (lo (bitwise-and value #xFF)))
    (i2c-write bus #x60 #x40 (bytevector hi lo))))

(dac-write! 2048)   ; mid-scale (~1.65V on a 3.3V supply)
(i2c-close bus)
```

## Example — servo control via PWM

```scheme
(import (curry rpi))

(define pwm (pwm-open 0 0))

(define (servo-angle! deg)          ; 0-180 degrees
  (let* ((min-ns  500000)           ; 0.5ms
         (max-ns 2500000)           ; 2.5ms
         (duty (+ min-ns (inexact->exact
                           (round (* (/ deg 180.0)
                                     (- max-ns min-ns)))))))
    (pwm-set! pwm 20000000 duty)))

(pwm-enable! pwm)
(servo-angle! 90)    ; centre
(sleep 1)
(servo-angle! 0)
(sleep 1)
(servo-angle! 180)
(sleep 1)
(pwm-disable! pwm)
(pwm-close pwm)
```
