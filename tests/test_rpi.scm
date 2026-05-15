;;; rpi module tests — GPIO, I2C, SPI, PWM.
;;;
;;; Hardware sections skip gracefully when the device node is absent or
;;; access is denied, so the test exits 0 on any machine without Pi hardware.
;;;
;;; Sections tested without hardware (always run):
;;;   - predicate? returns #f for non-handles
;;;   - type errors raise exceptions
;;;
;;; Sections that require a device node (skipped if absent):
;;;   GPIO:  /dev/gpiochip0   — opens line 0 as input, reads it, closes
;;;   I2C:   /dev/i2c-1       — opens bus, checks handle predicates, closes
;;;   SPI:   /dev/spidev0.0   — opens bus, checks handle predicates, closes
;;;   PWM:   /sys/class/pwm/pwmchip0  — export ch 0, set period+duty, close

(import (curry rpi))

;;; ---- harness ----

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

;;; catch-false: evaluate thunk, return #f on any exception
(define-syntax catch-false
  (syntax-rules ()
    ((catch-false body ...)
     (guard (exn (#t #f))
       body ...))))

;;; ---- predicates on non-handles (no hardware needed) ----

(check "gpio? on fixnum"  (gpio? 42)    #f)
(check "gpio? on string"  (gpio? "x")   #f)
(check "i2c?  on fixnum"  (i2c?  42)    #f)
(check "i2c?  on #f"      (i2c?  #f)    #f)
(check "spi?  on fixnum"  (spi?  42)    #f)
(check "spi?  on list"    (spi?  '())   #f)
(check "pwm?  on fixnum"  (pwm?  42)    #f)
(check "pwm?  on string"  (pwm?  "pwm") #f)

;;; ---- type-error paths (no hardware needed) ----

(check "gpio-open bad chip"
  (catch-false (gpio-open "bad" 0 'input))
  #f)

(check "gpio-open bad direction"
  (catch-false (gpio-open 0 0 'sideways))
  #f)

(check "i2c-open bad bus"
  (catch-false (i2c-open "one"))
  #f)

(check "spi-open bad bus"
  (catch-false (spi-open "zero" 0 1000000 0))
  #f)

(check "pwm-open bad chip"
  (catch-false (pwm-open "zero" 0))
  #f)

;;; ---- GPIO (/dev/gpiochip0) ----

(let ((h (and (file-exists? "/dev/gpiochip0") (catch-false (gpio-open 0 0 'input)))))
  (if h
      (begin
        (check "gpio? on real handle"  (gpio? h) #t)
        (check "i2c?  on gpio handle"  (i2c?  h) #f)
        (check "spi?  on gpio handle"  (spi?  h) #f)
        (check "pwm?  on gpio handle"  (pwm?  h) #f)
        (let ((v (catch-false (gpio-read h))))
          (if (fixnum? v)
              (check "gpio-read returns 0 or 1" (or (= v 0) (= v 1)) #t)
              (skip! "gpio-read (line not exported or inaccessible)")))
        (gpio-close h))
      (skip! "GPIO section (/dev/gpiochip0 absent or permission denied)")))

;;; ---- I2C (/dev/i2c-1) ----

(let ((h (and (file-exists? "/dev/i2c-1") (catch-false (i2c-open 1)))))
  (if h
      (begin
        (check "i2c? on real handle"  (i2c?  h) #t)
        (check "gpio? on i2c handle"  (gpio? h) #f)
        (check "spi?  on i2c handle"  (spi?  h) #f)
        (check "pwm?  on i2c handle"  (pwm?  h) #f)
        ;; Can't do a real read without a known device on the bus; just close.
        (i2c-close h))
      (skip! "I2C section (/dev/i2c-1 absent or permission denied)")))

;;; ---- SPI (/dev/spidev0.0) ----

(let ((h (and (file-exists? "/dev/spidev0.0") (catch-false (spi-open 0 0 1000000 0)))))
  (if h
      (begin
        (check "spi? on real handle"  (spi?  h) #t)
        (check "gpio? on spi handle"  (gpio? h) #f)
        (check "i2c?  on spi handle"  (i2c?  h) #f)
        (check "pwm?  on spi handle"  (pwm?  h) #f)
        ;; Loopback transfer: 4 bytes out, 4 bytes back.
        ;; Only meaningful if MISO is wired to MOSI; check length either way.
        (let ((rx (catch-false (spi-transfer h (make-bytevector 4 0)))))
          (if rx
              (check "spi-transfer returns same length" (bytevector-length rx) 4)
              (skip! "spi-transfer (ioctl failed)")))
        (spi-close h))
      (skip! "SPI section (/dev/spidev0.0 absent or permission denied)")))

;;; ---- PWM (/sys/class/pwm/pwmchip0) ----

(let ((h (and (file-exists? "/sys/class/pwm/pwmchip0") (catch-false (pwm-open 0 0)))))
  (if h
      (begin
        (check "pwm? on real handle"  (pwm?  h) #t)
        (check "gpio? on pwm handle"  (gpio? h) #f)
        (check "i2c?  on pwm handle"  (i2c?  h) #f)
        (check "spi?  on pwm handle"  (spi?  h) #f)
        ;; 50 Hz servo signal: period=20ms duty=1.5ms (servo centre position)
        (check "pwm-set! 50 Hz"
          (catch-false (pwm-set! h 20000000 1500000) #t) #t)
        (check "pwm-enable!"
          (catch-false (pwm-enable! h) #t) #t)
        (check "pwm-disable!"
          (catch-false (pwm-disable! h) #t) #t)
        (pwm-close h))
      (skip! "PWM section (/sys/class/pwm/pwmchip0 absent or permission denied)")))

;;; ---- summary ----

(display "\n--- rpi module test results ---\n")
(display "PASS: ") (display pass) (newline)
(display "FAIL: ") (display fail) (newline)
(display "SKIP: ") (display skip) (newline)

(if (> fail 0)
    (error "rpi tests failed"))
