# module: `(curry netcdf)`

NetCDF classic format reader — the self-contained, documented binary
format used for gridded scientific data (climate, oceanography, and
similar time/space datasets).

## Prerequisites

Pure Scheme — no C module, no build step, no external library. Works
everywhere curry runs.

## Scope

Supports NetCDF classic (CDF-1) and 64-bit-offset (CDF-2) files — read
only, no writer. Does **not** support NetCDF-4 (which is HDF5-backed —
use `(curry hdf5)` for those files instead) or CDF-5 (64-bit data, a
distinct on-disk layout). Both fixed-size and record (unlimited-dimension)
variables are supported, including files with multiple interleaved record
variables.

## API

```scheme
(import (curry netcdf))

(netcdf-open path)                  ; -> handle
(netcdf-variable nc name)           ; -> (values tensor dim-names)
(netcdf-attributes nc [var-name])   ; -> alist; global attrs if var-name omitted
(netcdf-dimensions nc)              ; -> alist of (name . length); #f length = unlimited
(netcdf-close nc)
```

`netcdf-open` parses the header only — variable data is read lazily, one
`netcdf-variable` call at a time, so opening a file with large variables
doesn't load them into memory until asked for.

For a record variable, `netcdf-variable` returns a tensor whose first
dimension is the current record count (`numrecs`), with the remaining
dimensions from the variable's declared shape.

## Example

```scheme
(import (curry netcdf))

(define nc (netcdf-open "data.nc"))
(display (netcdf-dimensions nc)) (newline)

(call-with-values
  (lambda () (netcdf-variable nc "temperature"))
  (lambda (temp dim-names)
    (display (tensor-shape temp)) (newline)
    (display (tensor-ref temp 0 0)) (newline)))

(display (netcdf-attributes nc "temperature")) (newline)
(netcdf-close nc)
```

## See also

- `docs/reference/module-fits.md` — FITS image reader/writer
- `docs/reference/module-hdf5.md` — HDF5 FFI wrapper (for NetCDF-4 files)
