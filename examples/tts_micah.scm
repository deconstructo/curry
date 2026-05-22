#!/usr/bin/env curry
;;; Recite Micah 6:7–8 via eSpeak NG.
;;;
;;; Usage:  ./build/curry examples/tts_micah.scm
;;;         ./build/curry examples/tts_micah.scm --slow
;;;         ./build/curry examples/tts_micah.scm --fast

(import (curry tts))

(define args (if (pair? command-line-args) command-line-args '()))

(define rate
  (cond ((member "--slow" args) 120)
        ((member "--fast" args) 220)
        (else                  150)))

(tts-set-rate!   rate)
(tts-set-pitch!  45)    ; slightly lower than default for gravitas
(tts-set-volume! 120)

(define verses
  '("Micah, chapter six, verses seven and eight."
    "Will the LORD be pleased with thousands of rams,"
    "or with ten thousands of rivers of oil?"
    "Shall I give my firstborn for my transgression,"
    "the fruit of my body for the sin of my soul?"
    "He hath showed thee, O man, what is good;"
    "and what doth the LORD require of thee,"
    "but to do justly,"
    "and to love mercy,"
    "and to walk humbly with thy God?"))

(for-each (lambda (line)
            (display line) (newline)
            (tts-speak line))
          verses)
