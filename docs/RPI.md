# Curry Scheme on Raspberry Pi

This guide covers building Curry on a Raspberry Pi and using the
`(curry rpi)` hardware module for GPIO, I2C, SPI, and PWM.

For the full procedure API see [docs/module-rpi.md](module-rpi.md).

---

## Supported boards

Any board running a 64-bit or 32-bit Linux kernel with the standard
`/dev/gpiochipN`, `/dev/i2c-N`, `/dev/spidevN.M`, and
`/sys/class/pwm` interfaces.  This includes:

- Raspberry Pi 3, 4, 5 (tested)
- Raspberry Pi Zero 2 W
- Orange Pi, Radxa Rock, and other Armbian/Debian-based boards

---

## 1. Install build dependencies on the Pi

```bash
sudo apt update
sudo apt install \
    build-essential cmake \
    libgc-dev libgmp-dev \
    libgpiod-dev \
    libreadline-dev
```

Optional modules also available on Pi:

```bash
sudo apt install \
    libsqlite3-dev libssl-dev \
    libcurl4-openssl-dev \
    libgit2-dev libpng-dev libjpeg-dev \
    libpaho-mqtt-dev
```

---

## 2. Enable hardware interfaces

Run `sudo raspi-config` and enable what you need under **Interface Options**:

| Interface | raspi-config option |
|-----------|-------------------|
| I2C | Interface Options → I2C |
| SPI | Interface Options → SPI |
| PWM | Edit `/boot/firmware/config.txt` — add `dtoverlay=pwm` |

Then reboot.

---

## 3. Add yourself to the hardware groups

```bash
sudo usermod -aG gpio,i2c,spi $USER
```

Log out and back in (or `newgrp gpio`) for the change to take effect.

For PWM via sysfs you may need root, or a udev rule:

```bash
# /etc/udev/rules.d/99-pwm.rules
SUBSYSTEM=="pwm*", PROGRAM="/bin/sh -c 'chown -R root:gpio /sys/class/pwm && chmod -R 770 /sys/class/pwm'"
```

---

## 4. Build Curry with the rpi module

```bash
git clone https://github.com/deconstructo/curry.git
cd curry

cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_MODULE_RPI=ON

cmake --build build -j$(nproc)

# Optional: install system-wide
sudo cmake --install build
```

The rpi module is **Linux-only** and is off by default — it must be
explicitly enabled with `-DBUILD_MODULE_RPI=ON`.  If `libgpiod-dev` is
missing, CMake will report a fatal error rather than silently skipping.

---

## 5. Verify the build

```bash
./build/curry -e '
(import (curry rpi))
(display "rpi module loaded OK") (newline)
(display (gpio? 42))   ; => #f
(display (i2c?  42))   ; => #f
(newline)
'
```

---

## 6. First examples

### Blink an LED (GPIO)

Wire an LED and 330Ω resistor between BCM pin 17 and GND.

```scheme
(import (curry rpi))

(define led (gpio-open 0 17 'output))

(let loop ((i 0))
  (when (< i 6)
    (gpio-write led (modulo i 2))
    (sleep 1)
    (loop (+ i 1))))

(gpio-close led)
```

### Read a button (GPIO)

Wire a button between BCM pin 27 and 3.3V with a 10kΩ pull-down to GND.

```scheme
(import (curry rpi))

(define btn (gpio-open 0 27 'input))

(let loop ()
  (display (gpio-read btn))
  (display " ")
  (sleep 0.1)
  (loop))
```

### Read a temperature sensor over I2C

Example with an MCP9808 (address `0x18`):

```scheme
(import (curry rpi))

(define bus (i2c-open 1))

(define (mcp9808-read-temp)
  (let* ((raw  (i2c-read bus #x18 #x05 2))
         (msb  (bytevector-u8-ref raw 0))
         (lsb  (bytevector-u8-ref raw 1))
         (raw16 (bitwise-ior (arithmetic-shift (bitwise-and msb #x1F) 8) lsb))
         (temp  (if (zero? (bitwise-and raw16 #x1000))
                    (* raw16 0.0625)
                    (* (- raw16 #x2000) 0.0625))))
    temp))

(display (mcp9808-read-temp))
(display "°C") (newline)

(i2c-close bus)
```

### Servo via PWM

Hardware PWM on pin 12 (PWM0).  Requires `dtoverlay=pwm` in
`/boot/firmware/config.txt`.

```scheme
(import (curry rpi))

(define servo (pwm-open 0 0))

(define (set-angle! deg)
  (let* ((min-us  500)
         (max-us 2500)
         (duty-us (+ min-us (inexact->exact
                              (round (* (/ deg 180.0) (- max-us min-us))))))
         (duty-ns (* duty-us 1000)))
    (pwm-set! servo 20000000 duty-ns)))

(pwm-enable! servo)

(for-each (lambda (angle)
            (display angle) (display "° ") (newline)
            (set-angle! angle)
            (sleep 1))
          '(0 45 90 135 180 90))

(pwm-disable! servo)
(pwm-close servo)
```

### SPI — reading an MCP3008 ADC

```scheme
(import (curry rpi))

(define spi (spi-open 0 0 1000000 0))   ; /dev/spidev0.0, 1 MHz, mode 0

(define (mcp3008-read channel)          ; channel 0-7
  (let* ((cmd (bytevector 1
                           (bitwise-ior #x80 (arithmetic-shift channel 4))
                           0))
         (rx  (spi-transfer spi cmd))
         (hi  (bitwise-and (bytevector-u8-ref rx 1) #x03))
         (lo  (bytevector-u8-ref rx 2)))
    (bitwise-ior (arithmetic-shift hi 8) lo)))

(display (mcp3008-read 0))   ; read channel 0 (0-1023)
(newline)

(spi-close spi)
```

---

## 7. Running as a script

```bash
./build/curry examples/blink.scm
```

Or make the script executable:

```scheme
#!/usr/bin/env curry
(import (curry rpi))
; ...
```

```bash
chmod +x blink.scm
./blink.scm
```

---

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `cannot open /dev/gpiochip0` | Not in gpio group | `sudo usermod -aG gpio $USER`, re-login |
| `cannot open /dev/i2c-1` | I2C not enabled or not in i2c group | `raspi-config` → enable I2C; `usermod -aG i2c` |
| `cannot open /dev/spidev0.0` | SPI not enabled | `raspi-config` → enable SPI |
| `sysfs node did not appear` | PWM overlay not loaded | Add `dtoverlay=pwm` to `/boot/firmware/config.txt`, reboot |
| `line request failed: Device or resource busy` | Another process holds the GPIO line | Check `gpioinfo` for claimed lines |

Inspect the GPIO chip and current line states:

```bash
gpiodetect          # list chips
gpioinfo gpiochip0  # show all lines and their current consumers
i2cdetect -y 1      # scan I2C bus 1 for devices
```
