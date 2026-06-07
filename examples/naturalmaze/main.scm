#!/usr/bin/env curry
;;; main.scm — Natural Maze entry point
;;;
;;; Run (from project root):
;;;   ./build/curry examples/naturalmaze/main.scm
;;;
;;; Or from this directory:
;;;   curry main.scm
;;;
;;; Compile each file to bytecode:
;;;   make -C examples/naturalmaze
;;;   ./build/curry examples/naturalmaze/main.scm   (auto-picks .scc)

(import (curry qt6))

;;; Locate this file's directory so (load ...) works regardless of CWD
(define *base*
  (let ((script (if (pair? command-line-args) (car command-line-args) "main.scm")))
    (let loop ((i (- (string-length script) 1)))
      (cond
        ((< i 0) "")
        ((or (char=? (string-ref script i) #\/)
             (char=? (string-ref script i) #\\))
         (substring script 0 (+ i 1)))
        (else (loop (- i 1)))))))

(define (load-module name)
  (let* ((base  *base*)
         (scc   (string-append base name ".scc"))
         (scm   (string-append base name ".scm")))
    (if (file-exists? scc)
        (load scc)
        (load scm))))

(load-module "world")
(load-module "player")
(load-module "shaders")
(load-module "renderer")
(load-module "ui")

;;; ── Start ────────────────────────────────────────────────────────────────

(generate-maze!)
(setup-ui!)
(run-event-loop)
