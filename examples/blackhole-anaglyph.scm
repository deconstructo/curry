#!/usr/bin/env curry
;;; blackhole-anaglyph.scm — examples/blackhole.scm in red/cyan 3D
;;;
;;; Wear red/cyan glasses, red over the LEFT eye.  The two eyes are placed a
;;; little apart in the falling observer's own frame, and each traces its
;;; own rays through the curved spacetime, so depth comes from the real
;;; light paths: the lensed image of the far side of the disk floats at a
;;; different depth from the near side.  Press A to switch to the normal
;;; view and back; all other keys are as in blackhole.scm.

(define blackhole-start-anaglyph #t)
(load "blackhole.scm")
