# Module: `(curry rpi)`

*v1.0.0 — 2026-05-28*

Raspberry Pi and Linux embedded hardware — GPIO, I2C, SPI, PWM, V4L2 camera,
UART, 1-Wire, hardware watchdog, and board detection.

**Platform:** Linux only. Compiled when `-DBUILD_MODULE_RPI=ON` and target is Linux.

## Import

```scheme
(import (curry rpi))
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MODULE_RPI=ON
cmake --build build -j$(nproc)

# Dependency
sudo apt install libgpiod-dev
```

---

## GPIO — `libgpiod`

Uses the modern Linux GPIO character device (`/dev/gpiochipN`).

```scheme
; Open a GPIO line.  dir: 'input | 'output
(gpio-open chip-num pin-num dir)   ; → handle

(gpio-read  handle)                ; → 0 or 1
(gpio-write handle value)
(gpio-close handle)
(gpio?      v)

; Synchronous edge wait.  Returns 'rising, 'falling, or #f on timeout.
(gpio-wait-edge handle)
(gpio-wait-edge handle timeout-ms)

; Asynchronous edge watcher (background thread).
(gpio-watch   handle proc)         ; proc receives 'rising or 'falling
(gpio-unwatch watcher)
(watcher?     v)
```

### Example — blink and interrupt

```scheme
(import (curry rpi))

; Blink LED on BCM4 five times
(define led (gpio-open 0 4 'output))
(let loop ((i 0))
  (when (< i 5)
    (gpio-write led 1) (sleep 0.5)
    (gpio-write led 0) (sleep 0.5)
    (loop (+ i 1))))
(gpio-close led)

; Watch button on BCM17 for 30 s
(define btn (gpio-open 0 17 'input))
(define w   (gpio-watch btn
               (lambda (edge)
                 (display (symbol->string edge)) (newline))))
(sleep 30)
(gpio-unwatch w)
(gpio-close btn)
```

---

## I2C

Direct `ioctl` on `/dev/i2c-N`.

```scheme
(i2c-open  bus-num)                    ; → handle  (/dev/i2c-N)
(i2c-read  handle addr reg nbytes)     ; → bytevector
(i2c-write handle addr reg bvec)
(i2c-close handle)
(i2c?      v)
```

### Example — MPU-6050 accelerometer

```scheme
(import (curry rpi))

(define imu (i2c-open 1))
(i2c-write imu #x68 #x6B (bytevector 0))   ; wake up MPU-6050

(define raw (i2c-read imu #x68 #x3B 6))    ; ACCEL_XOUT_H
(define ax  (/ (+ (* (bytevector-u8-ref raw 0) 256)
                   (bytevector-u8-ref raw 1))
               16384.0))                    ; ±2g range
(display (string-append "Ax = " (number->string ax) " g")) (newline)
(i2c-close imu)
```

---

## SPI

Direct `ioctl` on `/dev/spidevN.M`.

```scheme
(spi-open     bus device speed-hz mode)  ; → handle  (mode 0–3)
(spi-transfer handle tx-bvec)            ; → rx-bytevector (full-duplex)
(spi-close    handle)
(spi?         v)
```

### Example — MCP3008 ADC

```scheme
(import (curry rpi))

(define adc (spi-open 0 0 1350000 0))

(define (mcp3008-read ch)
  (let* ((cmd (bytevector #x01 (+ #x80 (* ch 16)) #x00))
         (rx  (spi-transfer adc cmd)))
    (+ (* (bitwise-and (bytevector-u8-ref rx 1) #x03) 256)
       (bytevector-u8-ref rx 2))))

(display (string-append "CH0 = " (number->string (mcp3008-read 0)))) (newline)
(spi-close adc)
```

---

## PWM — sysfs `/sys/class/pwm`

```scheme
(pwm-open    chip channel)             ; → handle
(pwm-set!    handle period-ns duty-ns)
(pwm-enable! handle)
(pwm-disable! handle)
(pwm-close   handle)
(pwm?        v)
```

### Enable hardware PWM on Pi

```bash
# /boot/config.txt:   dtoverlay=pwm,pin=18,func=2
```

### Example — servo neutral position

```scheme
(import (curry rpi))

(define servo (pwm-open 0 0))
(pwm-set! servo 20000000 1500000)   ; 20 ms period, 1.5 ms = neutral
(pwm-enable! servo)
(sleep 2)
(pwm-disable! servo)
(pwm-close servo)
```

---

## Camera — V4L2

Supports any V4L2-compatible capture device (Pi Camera Module 1/2,
USB webcams, CSI cameras via `bcm2835-v4l2`).

```scheme
; Open device and start streaming immediately.
; format: 'yuyv (default) | 'mjpeg | 'rgb24 | 'grey | 'h264
(camera-open path width height)
(camera-open path width height format)   ; → handle

; Capture one frame (blocks up to 1 s).
; Returns raw pixel data:
;   'yuyv  → 2 bytes/pixel (Y₀ U Y₁ V per 2 pixels)
;   'mjpeg → JPEG payload  (pass to (curry image) to decode)
;   'rgb24 → 3 bytes/pixel (R G B)
;   'grey  → 1 byte/pixel  (luminance Y)
(camera-capture handle)     ; → bytevector

(camera-width  handle)      ; → fixnum
(camera-height handle)      ; → fixnum
(camera-format handle)      ; → symbol
(camera-close  handle)
(camera?       v)
```

### Enable Pi Camera V4L2

```bash
# Pi Camera Module 1/2 (legacy stack):
sudo modprobe bcm2835-v4l2

# /boot/config.txt permanent:
# start_x=1
# gpu_mem=128
# Add to /etc/modules: bcm2835-v4l2

# Verify:
v4l2-ctl --list-devices
```

### Example — capture JPEG and save PNG

```scheme
(import (curry rpi))
(import (curry image))

(define cam   (camera-open "/dev/video0" 1280 720 'mjpeg))
(define frame (camera-capture cam))
(camera-close cam)

(image-save (image-from-jpeg-bytes frame) "/tmp/snapshot.png")
(display "Saved /tmp/snapshot.png") (newline)
```

### YUYV frame processing

YUYV packs 4 bytes per 2 pixels: `[Y₀ U Y₁ V]`. To extract luminance:

```scheme
(define (yuyv-luminance bvec)
  ;; Mean Y value: every other byte starting at 0
  (let* ((n   (bytevector-length bvec))
         (sum (let lp ((i 0) (s 0))
                (if (>= i n) s
                    (lp (+ i 2) (+ s (bytevector-u8-ref bvec i)))))))
    (/ (exact->inexact sum) (/ n 2.0))))
```

---

## UART / serial port — `termios`

```scheme
; Open at baud rate with 8N1 raw mode.
; Supported baud: 1200 2400 4800 9600 19200 38400 57600 115200 230400 460800 921600
(uart-open path baud)             ; → handle

(uart-read      handle n)         ; → bytevector  (blocks until n bytes)
(uart-write     handle bvec)
(uart-read-line handle)           ; → string | #f  (reads until '\n', 1 s timeout)
(uart-read-line handle timeout-ms)
(uart-available? handle)          ; → fixnum  (bytes in buffer, non-blocking)
(uart-close     handle)
(uart?          v)
```

### Enable GPIO UART

```bash
# /boot/config.txt:
# enable_uart=1
# dtoverlay=disable-bt      # free UART0 from Bluetooth
#
# Device:  /dev/ttyAMA0  (Pi 3/4/5 primary UART)
#          /dev/ttyS0    (mini-UART, lower quality)
```

### Example — GPS NMEA sentences

```scheme
(import (curry rpi))

(define gps (uart-open "/dev/ttyAMA0" 9600))
(let loop ((i 0))
  (when (< i 10)
    (let ((line (uart-read-line gps 2000)))
      (when line (display line) (newline))
      (loop (+ i 1)))))
(uart-close gps)
```

---

## 1-Wire — DS18B20 temperature sensor

Uses the Linux `w1-therm` kernel driver via sysfs.

```scheme
(w1-devices)         ; → list of path strings  (/sys/bus/w1/devices/28-xxxx)
(w1-temperature path); → flonum (°C)
(w1-raw path)        ; → string (raw w1_slave content)
```

### Enable 1-Wire

```bash
# /boot/config.txt:   dtoverlay=w1-gpio
# Connect DS18B20 DATA to GPIO4 (pin 7) with 4.7 kΩ pull-up to 3.3 V
```

### Example

```scheme
(import (curry rpi))

(for-each (lambda (d)
            (display (string-append d ": "
                                    (number->string (w1-temperature d))
                                    " °C"))
            (newline))
          (w1-devices))
```

---

## Hardware watchdog

```scheme
; Open /dev/watchdog; optional timeout in seconds.
; Starts counting immediately — kick soon!
(watchdog-open)
(watchdog-open timeout-secs)   ; → handle

; Reset the timer (call more often than the timeout to prevent reboot).
(watchdog-kick handle)

; Read the current hardware timeout in seconds.
(watchdog-timeout handle)      ; → fixnum

; Disarm (write magic 'V') and close.  System will NOT reboot.
; Omit this call (or let the process crash) for a guaranteed reboot.
(watchdog-close handle)
(watchdog? v)
```

### Example — supervised service

```scheme
(import (curry rpi))
(import (curry actors))

(define wdt (watchdog-open 30))
(display (string-append "WDT timeout: "
                        (number->string (watchdog-timeout wdt)) " s")) (newline)

; Background heartbeat — kick every 15 s
(spawn (lambda ()
         (let loop ()
           (sleep 15)
           (watchdog-kick wdt)
           (loop))))

; Main work here ...

(watchdog-close wdt)
```

---

## Board detection

```scheme
(rpi-model)       ; → string  "Raspberry Pi 4 Model B Rev 1.4" | #f
(rpi-serial)      ; → string  (16-hex-digit serial number) | #f
(rpi-memory-mb)   ; → fixnum  (total RAM in MiB)
(rpi-os-info)     ; → alist   ((kernel . "6.1.21-v8+") (distro . "Raspbian..."))
```

### Example

```scheme
(import (curry rpi))

(display (rpi-model))      (newline)   ; "Raspberry Pi 4 Model B Rev 1.4"
(display (rpi-memory-mb))  (newline)   ; 4096
(display (rpi-serial))     (newline)   ; "10000000abcdef01"
(for-each (lambda (kv) (display (car kv)) (display ": ")
                       (display (cdr kv)) (newline))
          (rpi-os-info))
; kernel: 6.1.21-v8+
; distro: Raspbian GNU/Linux 12 (bookworm)
```

---

## Hardware testing guide

### Prerequisites

| Interface | Kernel module / config | Package |
|---|---|---|
| GPIO | Built-in | `libgpiod-dev` (build time) |
| I2C | `sudo raspi-config` → I2C | none |
| SPI | `sudo raspi-config` → SPI | none |
| Camera | `bcm2835-v4l2` or `rpicam` | none |
| UART | `/boot/config.txt`: `enable_uart=1` | none |
| 1-Wire | `/boot/config.txt`: `dtoverlay=w1-gpio` | none |
| PWM | `/boot/config.txt`: `dtoverlay=pwm,pin=18,func=2` | none |
| Watchdog | Always available | none |

### Smoke tests (run on Pi)

```bash
# GPIO: blink LED on BCM4
./build/curry -e '(import (curry rpi))
  (define led (gpio-open 0 4 '"'"'output))
  (gpio-write led 1) (sleep 0.5) (gpio-write led 0)
  (gpio-close led) (display "GPIO OK\n")'

# I2C: open bus 1
./build/curry -e '(import (curry rpi))
  (define b (i2c-open 1)) (i2c-close b) (display "I2C OK\n")'

# SPI: loopback (connect MOSI pin 19 → MISO pin 21)
./build/curry -e '(import (curry rpi))
  (define s (spi-open 0 0 1000000 0))
  (define tx (bytevector #xAB #xCD))
  (display (equal? (bytevector->list (spi-transfer s tx))
                   (list #xAB #xCD)))
  (spi-close s) (newline)'

# Camera
./build/curry -e '(import (curry rpi))
  (define c (camera-open "/dev/video0" 640 480 '"'"'yuyv))
  (define f (camera-capture c))
  (display (bytevector-length f)) (newline)
  (camera-close c)'

# 1-Wire DS18B20
./build/curry -e '(import (curry rpi))
  (for-each (lambda (d) (display (w1-temperature d)) (newline))
            (w1-devices))'

# Board info
./build/curry -e '(import (curry rpi))
  (display (rpi-model)) (newline)
  (display (rpi-memory-mb)) (display " MiB\n")'
```

### Simulation-mode tests (any platform)

```bash
./build/curry tests/rpi_tests.scm
```

These tests verify the Scheme API surface, mathematical helpers (YUYV
processing, NMEA parsing), and the controller synthesis pipeline without
requiring any hardware.

---

## Procedure index

| Procedure | Description |
|---|---|
| `gpio-open chip pin dir` | Open GPIO line |
| `gpio-read h` | Read logic level (0/1) |
| `gpio-write h v` | Write logic level |
| `gpio-close h` | Release line |
| `gpio-wait-edge h [ms]` | Synchronous edge wait |
| `gpio-watch h proc` | Async edge callback |
| `gpio-unwatch w` | Stop watcher |
| `i2c-open bus` | Open I2C bus |
| `i2c-read h addr reg n` | Read n bytes from register |
| `i2c-write h addr reg bv` | Write to register |
| `spi-open bus dev hz mode` | Open SPI device |
| `spi-transfer h tx` | Full-duplex transfer |
| `pwm-open chip ch` | Open PWM channel |
| `pwm-set! h period-ns duty-ns` | Set timing |
| `pwm-enable! h` | Start output |
| `pwm-disable! h` | Stop output |
| `camera-open path w h [fmt]` | Open V4L2 device |
| `camera-capture h` | Capture one frame |
| `camera-width/height/format h` | Accessors |
| `uart-open path baud` | Open serial port |
| `uart-read h n` | Read n bytes |
| `uart-write h bv` | Write bytes |
| `uart-read-line h [ms]` | Read line (string) |
| `uart-available? h` | Bytes waiting |
| `w1-devices` | List sensor paths |
| `w1-temperature path` | °C temperature |
| `w1-raw path` | Raw sysfs string |
| `watchdog-open [secs]` | Open watchdog |
| `watchdog-kick h` | Pet the dog |
| `watchdog-timeout h` | Current timeout |
| `watchdog-close h` | Disarm and close |
| `rpi-model` | Board model string |
| `rpi-serial` | Board serial string |
| `rpi-memory-mb` | RAM in MiB |
| `rpi-os-info` | Kernel/distro alist |
