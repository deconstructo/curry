# Module: (curry image)

PNG, JPEG, and GIF image loading, saving, and pixel manipulation.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libpng-dev libjpeg-dev

# macOS
brew install libpng libjpeg
```

Enable: `-DBUILD_MODULE_IMAGE=ON` (default on). At least one of libpng or libjpeg must be found. GIF reading is built-in (no extra library required). GIF saving is not supported.

## Import

```scheme
(import (curry image))
```

## Image representation

An image is a vector `#(width height channels bytevector)`:
- `channels` is 3 (RGB) or 4 (RGBA)
- `bytevector` contains `width × height × channels` bytes in row-major, top-to-bottom order
- Pixel `(x, y)` channel `c` is at byte offset `(y * width + x) * channels + c`

## I/O

```scheme
(image-load path)            ; → image | #f   (PNG, JPEG, or GIF by extension/magic)
(image-save path image)      ; → void         (PNG or JPEG by extension)
(image-format path)          ; → symbol: png | jpeg | gif | unknown
```

JPEG quality is fixed at 85 on save. PNG saves at default compression.

## Construction

```scheme
(image-make width height channels)   ; → image (zeroed, channels = 3 or 4)
```

## Accessors

```scheme
(image-width    image)   ; → integer
(image-height   image)   ; → integer
(image-channels image)   ; → integer (3 or 4)
(image-pixels   image)   ; → bytevector (raw pixel data, mutable)

(image-ref  image x y channel)       ; → integer [0, 255]
(image-set! image x y channel value) ; → void
```

## Transforms

All transforms return a new image; the original is unchanged.

```scheme
(image-crop  image x y width height)    ; → image
(image-scale image new-width new-height) ; → image  (bilinear interpolation)
(image-flip-horizontal image)            ; → image
(image-flip-vertical   image)            ; → image
(image-grayscale       image)            ; → 3-channel image (Rec.601 luma)
```

## Examples

### Load, process, save

```scheme
(import (curry image))

(define img (image-load "photo.jpg"))
(display (image-width img)) (display "x") (display (image-height img)) (newline)

; Convert to grayscale and save
(define gray (image-grayscale img))
(image-save "photo-gray.png" gray)

; Crop the centre third
(define w (image-width img))
(define h (image-height img))
(define cropped (image-crop img
  (quotient w 3) (quotient h 3)
  (quotient w 3) (quotient h 3)))
(image-save "centre.png" cropped)

; Scale to thumbnail
(define thumb (image-scale img 128 128))
(image-save "thumb.png" thumb)
```

### Draw into an image (procedural)

```scheme
(import (curry image))

(define img (image-make 256 256 3))

; Draw a red gradient
(let loop-y ((y 0))
  (when (< y 256)
    (let loop-x ((x 0))
      (when (< x 256)
        (image-set! img x y 0 x)           ; R = x
        (image-set! img x y 1 y)           ; G = y
        (image-set! img x y 2 128)         ; B = constant
        (loop-x (+ x 1))))
    (loop-y (+ y 1))))

(image-save "gradient.png" img)
```

### Render simulation frames

```scheme
(import (curry image))
(import (curry ui))

; In a canvas draw callback, extract a snapshot of the simulation
; and save it as a PNG frame for video encoding:
(define (save-frame! n cr w h)
  (let ((img (image-make w h 3)))
    ; Read pixels from the Cairo surface (requires cr-to-image, not yet exposed)
    ; Alternative: use image-make + procedural drawing for offline rendering
    (image-save (string-append "frame-" (number->string n) ".png") img)))
```

## Notes

- PNG images with transparency are loaded as 4-channel RGBA. JPEG images are always 3-channel RGB.
- GIF loading decodes only the first frame. Animated GIFs are partially supported (first frame only).
- `image-pixels` returns the live bytevector inside the image; mutating it directly updates the image.
- There is no compositing API yet; for overlaying images, iterate pixels manually.
