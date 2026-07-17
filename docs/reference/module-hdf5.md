# module: `(curry hdf5)`

FFI wrapper over `libhdf5`'s high-level (H5LT) and core API — a
lightweight, ergonomic reader/writer for HDF5 datasets and attributes.

## Prerequisites

`(curry hdf5)` is a pure Scheme library built on `(curry ffi)` — it
`dlopen`s `libhdf5` **at runtime**, not link time, so there is no
`BUILD_MODULE_HDF5` CMake flag; only `BUILD_FFI=ON` (for the underlying
general FFI) is required at build time. libhdf5 itself must be installed
on the machine actually running the code:

```bash
# macOS
brew install hdf5

# Debian/Ubuntu
apt install libhdf5-dev

# Fedora/RHEL
dnf install hdf5-devel
```

If libhdf5 can't be found, `(import (curry hdf5))` raises a clear error
naming the install command for your platform rather than a bare `dlopen`
failure. The module probes a list of candidate library paths (plain
`libhdf5.dylib`/`.so`, then Homebrew's and Debian's non-standard
locations) since HDF5's shared-library naming is inconsistent across
distros.

## Scope

Simple double-precision datasets (read/write) and scalar/1-D string or
numeric attributes — a "nice wrapper" for the common scientific-data
case, not full HDF5 API coverage:

- No groups are created on write — the parent group of a dataset path
  must already exist (writing to the file root always works).
- No compression, chunking, or non-double element types on write.
- Reading a dataset created by another tool with a non-double element
  type is not supported.
- Both HDF5 string kinds are handled correctly on read: fixed-length
  (raw bytes) and variable-length (the h5py default, and increasingly
  the common case elsewhere) — see "Implementation notes" below if
  you're curious why this needed special handling.

## API

```scheme
(import (curry hdf5))

(hdf5-open path)                    ; -> handle (creates the file if absent)
(hdf5-close f)
(hdf5-read f dataset-path)          ; -> tensor
(hdf5-write f dataset-path tensor)
(hdf5-attributes f object-path)     ; -> alist (string or number values)
```

`hdf5-open` opens an existing file read-write, or creates a new one
(truncating) if the path doesn't exist yet — one call handles both cases.

## Example

```scheme
(import (curry hdf5))

(define f (hdf5-open "data.h5"))
(define t (make-tensor (list 100 100) 0.0))
;; ... fill t ...
(hdf5-write f "measurements/temperature" t)
(hdf5-close f)

(define f2 (hdf5-open "data.h5"))
(define temp (hdf5-read f2 "measurements/temperature"))
(display (tensor-shape temp)) (newline)
(display (hdf5-attributes f2 "measurements/temperature")) (newline)
(hdf5-close f2)
```

## Implementation notes

Two general-purpose FFI primitives were added to `(curry ffi)` to make
this wrapper possible — neither is HDF5-specific, and both are reusable
by any future FFI binding:

- **`with-pinned-bytevector`** — zero-copy pinning for a bytevector's raw
  bytes, mirroring the existing `with-pinned-matrix`/`with-pinned-tensor`.
  Needed for HDF5's non-double input arrays (dimension-size lists) and
  out-parameters (`H5LTget_dataset_ndims` etc. write results into a
  caller-supplied buffer).
- **`peek-bytes`** — copies N bytes starting at an arbitrary address into
  a fresh bytevector. Needed because HDF5 hands back variable-length
  string attributes as a raw pointer into its own heap memory (even
  through the "high-level" convenience API), which must be dereferenced
  and then freed (`H5free_memory`) rather than read into a
  caller-supplied buffer the way fixed-size results are.

See `docs/reference/module-ffi.md` for the full FFI primitive reference.

## See also

- `docs/reference/module-fits.md` — FITS image reader/writer
- `docs/reference/module-netcdf.md` — NetCDF classic reader (for NetCDF-4
  files, which are HDF5-backed, use this module instead)
- `docs/reference/module-ffi.md` — the general FFI layer this is built on
