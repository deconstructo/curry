# module: `(curry fits)`

FITS (Flexible Image Transport System) image reader/writer — the standard
scientific image format used across astronomy.

## Prerequisites

Pure Scheme — no C module, no build step, no external library. Works
everywhere curry runs.

## Scope

Covers the FITS primary-HDU image case: 2880-byte blocks of 80-character
ASCII header cards terminated by an `END` card, followed by big-endian
binary image data (also padded to a multiple of 2880 bytes). This is the
common scientific-data use case.

Not covered: multiple HDUs/extensions (`IMAGE`/`TABLE`/`BINTABLE`
extensions), random-groups records, checksums. Reading always returns the
primary HDU only.

## API

```scheme
(import (curry fits))

(fits-read-image path)              ; -> (values tensor header-alist)
(fits-write-image path tensor)
(fits-write-image path tensor #:header header-alist)
(fits-header-ref header key [default])   ; case-insensitive lookup
```

`fits-read-image` returns a tensor shaped from the file's `NAXISn` cards,
with `BSCALE`/`BZERO` linear rescaling already applied, and the full
header as an alist of `(keyword . value)` pairs (values are strings,
numbers, or booleans as parsed from the card; repeated keywords like
`HISTORY`/`COMMENT` all appear, in file order).

`fits-write-image` always writes `BITPIX=-64` (double-precision) data,
regardless of the tensor's actual value range — curry tensors are
double-only, so this is the lossless round-trip choice. Header keywords
must be 8 characters or fewer (the FITS limit); `fits-write-image` raises
a clear error rather than silently writing a malformed card if a keyword
is too long.

## Example

```scheme
(import (curry fits))

(define t (make-tensor (list 100 100) 0.0))
;; ... fill t with image data ...

(fits-write-image "output.fits" t
  #:header (list (cons "OBJECT" "M31") (cons "EXPTIME" 30)))

(call-with-values
  (lambda () (fits-read-image "output.fits"))
  (lambda (image header)
    (display (tensor-shape image)) (newline)
    (display (fits-header-ref header "OBJECT")) (newline)))
```

## See also

- `docs/reference/module-netcdf.md` — NetCDF classic reader
- `docs/reference/module-hdf5.md` — HDF5 FFI wrapper
