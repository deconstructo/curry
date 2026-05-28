#!/usr/bin/env curry
;;; pi_camera_demo.scm — V4L2 camera capture on Raspberry Pi
;;;
;;; Demonstrates:
;;;   (curry rpi) camera interface: open, capture, close
;;;   Basic frame analysis: luminance histogram, motion detection
;;;   Integration with (curry image) for JPEG save when available
;;;
;;; Hardware:
;;;   Pi Camera Module 1/2 (with v4l2 kernel module loaded):
;;;     sudo modprobe bcm2835-v4l2
;;;   Pi Camera Module 3 / libcamera devices:
;;;     use 'media-ctl' or 'v4l2-ctl --list-devices' to find /dev/videoN
;;;   USB webcam: just works as /dev/video0
;;;
;;; Usage:
;;;   ./build/curry examples/pi_camera_demo.scm [device]
;;;   ./build/curry examples/pi_camera_demo.scm /dev/video0
;;;
;;; This script is written to run on Linux/Pi only.
;;; On macOS it will print a diagnostic and exit cleanly.

(import (scheme base))
(import (scheme write))
(import (scheme inexact))

;;; ── Platform guard ──────────────────────────────────────────────────────

(define running-on-pi?
  ;; Check for /proc/device-tree/model — only present on embedded Linux
  (let ((f (open-input-file "/proc/device-tree/model")))
    (if f (begin (close-input-port f) #t) #f)))

;;; ── Camera utilities ─────────────────────────────────────────────────────

;;; Compute mean luminance of a YUYV frame.
;;; YUYV packs 4 bytes per 2 pixels: [Y0 U Y1 V].
;;; Y is the luminance byte; take every other byte starting at 0.
(define (yuyv-mean-luminance bvec)
  (let* ((n   (bytevector-length bvec))
         (sum (let loop ((i 0) (s 0))
                (if (>= i n) s
                    (loop (+ i 2) (+ s (bytevector-u8-ref bvec i))))))
         (npix (/ n 2)))
    (/ (inexact sum) (inexact npix))))

;;; Compute per-region (4×4 grid) mean luminance.
;;; Returns a 16-element vector of mean Y values.
(define (yuyv-region-means bvec width height)
  (let* ((bw (quotient width  4))   ; block width in pixels
         (bh (quotient height 4)))  ; block height in pixels
    (let lp-r ((r 0) (means '()))
      (if (= r 4)
          (list->vector (reverse means))
          (let lp-c ((c 0) (row-means means))
            (if (= c 4)
                (lp-r (+ r 1) row-means)
                (let block-sum ((y0 (* r bh)) (total 0) (count 0))
                  (if (= y0 (* (+ r 1) bh))
                      (lp-c (+ c 1)
                            (cons (if (= count 0) 0.0
                                      (/ (inexact total) (inexact count)))
                                  row-means))
                      ;; sum one row of the block
                      (let pix-sum ((x0 (* c bw)) (t total) (k count))
                        (if (= x0 (* (+ c 1) bw))
                            (block-sum (+ y0 1) t k)
                            (let* ((px   (+ (* y0 width) x0))
                                   (byte (+ (* px 2)))   ; YUYV: 2 bytes/pixel
                                   (lum  (if (< byte (bytevector-length bvec))
                                             (bytevector-u8-ref bvec byte)
                                             128)))
                              (pix-sum (+ x0 1) (+ t lum) (+ k 1)))))))))))))

;;; Motion detection: compare two region-mean vectors.
;;; Returns motion score (mean absolute difference across 16 blocks).
(define (motion-score means1 means2)
  (/ (let lp ((i 0) (s 0.0))
       (if (= i 16) s
           (lp (+ i 1) (+ s (abs (- (vector-ref means1 i)
                                    (vector-ref means2 i)))))))
     16.0))

;;; ── Main ────────────────────────────────────────────────────────────────

(define device (if (and (> (length command-line-args) 0)
                        (string? (car command-line-args)))
                   (car command-line-args)
                   "/dev/video0"))

(define width  640)
(define height 480)

(display "Pi camera demo") (newline)
(display (string-append "  Device: " device)) (newline)
(display (string-append "  Resolution: " (number->string width)
                        "×" (number->string height))) (newline)
(newline)

;;; Guard: only attempt hardware open on Linux
(define cam #f)

(define (run-demo)
  (import (curry rpi))

  (display "Board: ")
  (display (rpi-model))
  (newline)
  (display (string-append "RAM: " (number->string (rpi-memory-mb)) " MiB")) (newline)
  (newline)

  (display "Opening camera...") (newline)
  (set! cam (camera-open device width height 'yuyv))
  (display (string-append "  Format: "
                          (symbol->string (camera-format cam))
                          "  "
                          (number->string (camera-width cam))
                          "×"
                          (number->string (camera-height cam)))) (newline)

  ;; Warm up: discard first 5 frames (auto-exposure settling)
  (display "Warming up (5 frames)...") (newline)
  (let warmup ((i 0))
    (when (< i 5) (camera-capture cam) (warmup (+ i 1))))

  ;; Capture 10 frames and report motion
  (display "Capturing 10 frames, reporting motion...") (newline)
  (let* ((frame0      (camera-capture cam))
         (prev-means  (yuyv-region-means frame0 width height)))
    (let loop ((i 1) (prev-means prev-means))
      (when (<= i 10)
        (let* ((frame      (camera-capture cam))
               (lum        (yuyv-mean-luminance frame))
               (curr-means (yuyv-region-means frame width height))
               (motion     (motion-score prev-means curr-means)))
          (display (string-append "  Frame " (number->string i)
                                  ": lum=" (number->string (inexact lum))
                                  "  motion=" (number->string (inexact motion))))
          (when (> motion 5.0) (display "  ← MOTION DETECTED"))
          (newline)
          (loop (+ i 1) curr-means)))))

  (display "Closing camera.") (newline)
  (camera-close cam)
  (set! cam #f))

;;; Check if we're on a Linux system with V4L2 support
(define (linux?)
  (let ((f (open-input-file "/proc/version")))
    (if f (begin (close-input-port f) #t) #f)))

(if (linux?)
    (run-demo)
    (begin
      (display "Not running on Linux — camera hardware not available.") (newline)
      (display "To test: ssh into a Raspberry Pi and run this script directly.") (newline)))

;;; ── Sensor fusion hint ──────────────────────────────────────────────────
;;;
;;; Combining camera with IMU for visual-inertial odometry:
;;;
;;; (import (curry rpi))
;;; (define imu    (i2c-open 1))       ; MPU-6050
;;; (define camera (camera-open "/dev/video0" 640 480 'yuyv))
;;;
;;; (define (read-gyro)
;;;   ;; GYRO_XOUT_H register 0x43, 6 bytes for X,Y,Z
;;;   (let ((d (i2c-read imu #x68 #x43 6)))
;;;     (let* ((gz (+ (* (bytevector-u8-ref d 4) 256) (bytevector-u8-ref d 5))))
;;;       (/ (inexact gz) 131.0))))    ; 131 LSB/(°/s) for ±250°/s range
;;;
;;; (define (visual-inertial-step prev-frame)
;;;   (let* ((omega   (read-gyro))      ; rad/s from gyro
;;;          (frame   (camera-capture camera))
;;;          (motion  (motion-score (yuyv-region-means prev-frame 640 480)
;;;                                 (yuyv-region-means frame 640 480))))
;;;     (values frame omega motion)))
