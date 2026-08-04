;;; (curry hdf5) — FFI wrapper over libhdf5's high-level (H5LT) + core API.
;;;
;;; Requires libhdf5 installed on the *running* system (dlopen'd at import
;;; time, not linked at build time — no CMake flag needed, just BUILD_FFI=ON).
;;; Install: `brew install hdf5` (macOS), `apt install libhdf5-dev` (Debian/
;;; Ubuntu), `dnf install hdf5-devel` (Fedora/RHEL).
;;;
;;; Scope: simple double-precision datasets (read/write) and scalar/1-D
;;; string or numeric attributes. No groups created on write (the parent
;;; group of a dataset path must already exist), no compression/chunking,
;;; no non-double element types on write. Reading a dataset created by
;;; another tool with a non-double type is not supported either — this is
;;; a "nice wrapper" for the common scientific-data case, not full HDF5
;;; API coverage.
;;;
;;; API:
;;;   (hdf5-open path)                        -> handle (creates if absent)
;;;   (hdf5-close f)
;;;   (hdf5-read f dataset-path)               -> tensor
;;;   (hdf5-write f dataset-path tensor)
;;;   (hdf5-attributes f object-path)          -> alist (string or number values)

(define-library (curry hdf5)
  (import (curry ffi))
  (import (scheme base))
  (export
    hdf5-open hdf5-close hdf5-read hdf5-write hdf5-attributes)
  (begin

;; ── Library discovery ────────────────────────────────────────────────────────
;;
;; HDF5's shared-library naming/location is inconsistent across platforms;
;; probe a candidate list and use the first one that dlopens, rather than
;; assuming a single hardcoded path.

(define %hdf5-candidates
  (list
    "libhdf5.dylib"                                                  ; macOS, on loader path
    "libhdf5.so"                                                     ; Linux, on loader path
    "/opt/homebrew/opt/hdf5/lib/libhdf5.dylib"                       ; Homebrew, Apple Silicon
    "/usr/local/opt/hdf5/lib/libhdf5.dylib"                          ; Homebrew, Intel Mac
    "/usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5.so.103"           ; Debian/Ubuntu, x86_64
    "/usr/lib/aarch64-linux-gnu/hdf5/serial/libhdf5.so.103"          ; Debian/Ubuntu, arm64
    "/usr/lib64/libhdf5.so"))                                        ; Fedora/RHEL

(define %hdf5-hl-candidates
  (list
    "libhdf5_hl.dylib"
    "libhdf5_hl.so"
    "/opt/homebrew/opt/hdf5/lib/libhdf5_hl.dylib"
    "/usr/local/opt/hdf5/lib/libhdf5_hl.dylib"
    "/usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5_hl.so.100"
    "/usr/lib/aarch64-linux-gnu/hdf5/serial/libhdf5_hl.so.100"
    "/usr/lib64/libhdf5_hl.so"))

(define (%hdf5-try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

(define %hdf5-lib
  (or (%hdf5-try-load %hdf5-candidates)
      (error "hdf5: could not load libhdf5 — install it first:
  macOS:           brew install hdf5
  Debian/Ubuntu:   apt install libhdf5-dev
  Fedora/RHEL:     dnf install hdf5-devel")))

(define %hdf5-hl-lib
  (or (%hdf5-try-load %hdf5-hl-candidates)
      (error "hdf5: found libhdf5 but not libhdf5_hl (the high-level API) — it usually ships in the same package; check your installation")))

;; ── Constants ────────────────────────────────────────────────────────────────

(define %H5F-ACC-RDONLY 0)
(define %H5F-ACC-RDWR   1)
(define %H5F-ACC-TRUNC  2)
(define %H5P-DEFAULT    0)
(define %H5T-INTEGER    0)
(define %H5T-FLOAT      1)
(define %H5T-STRING     3)
(define %H5-INDEX-NAME  0)
(define %H5-ITER-INC    0)

;; ── Raw foreign bindings ─────────────────────────────────────────────────────
;; hid_t/herr_t are both fixed-width (int64_t / int32_t) per libhdf5's public
;; headers, not platform-dependent `long`, so int64/int32 marshal correctly.

(define-foreign (%H5Fopen (name c-string) (flags int) (fapl int64))
  → int64 #:from %hdf5-lib #:c-name "H5Fopen")
(define-foreign (%H5Fcreate (name c-string) (flags int) (fcpl int64) (fapl int64))
  → int64 #:from %hdf5-lib #:c-name "H5Fcreate")
(define-foreign (%H5Fclose (file_id int64)) → int #:from %hdf5-lib #:c-name "H5Fclose")

(define-foreign (%H5LTmake_dataset_double
                  (loc_id int64) (name c-string) (rank int)
                  (dims c-ptr) (buffer c-ptr))
  → int #:from %hdf5-hl-lib #:c-name "H5LTmake_dataset_double")
(define-foreign (%H5LTread_dataset_double
                  (loc_id int64) (name c-string) (buffer c-ptr))
  → int #:from %hdf5-hl-lib #:c-name "H5LTread_dataset_double")
(define-foreign (%H5LTget_dataset_ndims (loc_id int64) (name c-string) (rank c-ptr))
  → int #:from %hdf5-hl-lib #:c-name "H5LTget_dataset_ndims")
(define-foreign (%H5LTget_dataset_info
                  (loc_id int64) (name c-string) (dims c-ptr)
                  (class_id c-ptr) (type_size c-ptr))
  → int #:from %hdf5-hl-lib #:c-name "H5LTget_dataset_info")
(define-foreign (%H5LTfind_dataset (loc_id int64) (name c-string))
  → int #:from %hdf5-hl-lib #:c-name "H5LTfind_dataset")

(define-foreign (%H5Eset_auto2 (estack_id int64) (func c-ptr) (client_data c-ptr))
  → int #:from %hdf5-lib #:c-name "H5Eset_auto2")

(define-foreign (%H5Aopen_by_idx
                  (loc_id int64) (obj_name c-string) (idx_type int) (order int)
                  (n uint64) (aapl int64) (lapl int64))
  → int64 #:from %hdf5-lib #:c-name "H5Aopen_by_idx")
(define-foreign (%H5Aget_name (attr_id int64) (buf_size uint64) (buf c-ptr))
  → int64 #:from %hdf5-lib #:c-name "H5Aget_name")
(define-foreign (%H5Aget_type (attr_id int64)) → int64 #:from %hdf5-lib #:c-name "H5Aget_type")
(define-foreign (%H5Aget_space (attr_id int64)) → int64 #:from %hdf5-lib #:c-name "H5Aget_space")
(define-foreign (%H5Aread (attr_id int64) (mem_type_id int64) (buf c-ptr))
  → int #:from %hdf5-lib #:c-name "H5Aread")
(define-foreign (%H5Aclose (attr_id int64)) → int #:from %hdf5-lib #:c-name "H5Aclose")
(define-foreign (%H5Tget_class (type_id int64)) → int #:from %hdf5-lib #:c-name "H5Tget_class")
(define-foreign (%H5Tget_size (type_id int64)) → uint64 #:from %hdf5-lib #:c-name "H5Tget_size")
(define-foreign (%H5Tclose (type_id int64)) → int #:from %hdf5-lib #:c-name "H5Tclose")
(define-foreign (%H5Sget_simple_extent_npoints (space_id int64))
  → int64 #:from %hdf5-lib #:c-name "H5Sget_simple_extent_npoints")
(define-foreign (%H5Sclose (space_id int64)) → int #:from %hdf5-lib #:c-name "H5Sclose")
(define-foreign (%H5Tis_variable_str (type_id int64)) → int #:from %hdf5-lib #:c-name "H5Tis_variable_str")
(define-foreign (%H5free_memory (mem c-ptr)) → int #:from %hdf5-lib #:c-name "H5free_memory")

;; ── Native-endian scratch-buffer helpers ─────────────────────────────────────
;;
;; These marshal in-memory C values (hid_t/hsize_t/int arrays passed to or
;; from a C function in this process), NOT on-disk file formats — encoded
;; in the host's native byte order, unlike the big-endian file-format
;; helpers in (curry private binary-io). curry's supported platforms
;; (macOS/Linux on x86_64 and arm64) are all little-endian.

(define (%u64le-set! bv i v)
  (let loop ((k 0) (v v))
    (when (< k 8)
      (bytevector-u8-set! bv (+ i k) (bitwise-and v #xFF))
      (loop (+ k 1) (arithmetic-shift v -8)))))

(define (%u64le-ref bv i)
  (let loop ((k 7) (acc 0))
    (if (< k 0) acc
        (loop (- k 1) (+ (arithmetic-shift acc 8) (bytevector-u8-ref bv (+ i k)))))))

(define (%s32le-ref bv i)
  (let ((u (let loop ((k 3) (acc 0))
             (if (< k 0) acc
                 (loop (- k 1) (+ (arithmetic-shift acc 8) (bytevector-u8-ref bv (+ i k))))))))
    (if (>= u #x80000000) (- u #x100000000) u)))

;; ── Error checking ───────────────────────────────────────────────────────────

(define (%hdf5-check herr what)
  (when (< herr 0) (error (string-append "hdf5: " what " failed"))))

(define (%hdf5-check-id id what)
  (when (< id 0) (error (string-append "hdf5: " what " failed")))
  id)

;; ── Public: open / close ─────────────────────────────────────────────────────

;; HDF5's default error handler prints a full diagnostic trace to stderr on
;; every failed call — including the expected "no more attributes at this
;; index" failure hdf5-attributes uses to detect the end of the list.
;; Silence it once; callers get curry's own (error ...) messages instead.
(%H5Eset_auto2 0 (cptr-null) (cptr-null))

(define (hdf5-open path)
  (let ((id (if (file-exists? path)
                (%H5Fopen path %H5F-ACC-RDWR %H5P-DEFAULT)
                (%H5Fcreate path %H5F-ACC-TRUNC %H5P-DEFAULT %H5P-DEFAULT))))
    (%hdf5-check-id id "hdf5-open")
    (list (cons 'id id) (cons 'path path))))

(define (hdf5-close f)
  (%hdf5-check (%H5Fclose (cdr (assq 'id f))) "hdf5-close"))

;; ── Public: dataset read/write ───────────────────────────────────────────────

(define (hdf5-write f dataset-path tensor)
  (let* ((file-id (cdr (assq 'id f)))
         (shape (tensor-shape tensor))
         (rank (length shape))
         (dims-bv (make-bytevector (* rank 8) 0)))
    (let loop ((s shape) (i 0))
      (unless (null? s)
        (%u64le-set! dims-bv (* i 8) (car s))
        (loop (cdr s) (+ i 1))))
    (with-pinned-bytevector dims-bv dims-ptr
      (with-pinned-tensor tensor data-ptr
        (%hdf5-check
          (%H5LTmake_dataset_double file-id dataset-path rank dims-ptr data-ptr)
          (string-append "hdf5-write " dataset-path))))))

(define (hdf5-read f dataset-path)
  (let* ((file-id (cdr (assq 'id f))))
    (when (= (%H5LTfind_dataset file-id dataset-path) 0)
      (error "hdf5: no such dataset" dataset-path))
    (let ((rank-bv (make-bytevector 4 0)))
      (with-pinned-bytevector rank-bv rank-ptr
        (%hdf5-check (%H5LTget_dataset_ndims file-id dataset-path rank-ptr)
                     "hdf5-read (get ndims)"))
      (let* ((rank (%s32le-ref rank-bv 0))
             (dims-bv (make-bytevector (* rank 8) 0)))
        (with-pinned-bytevector dims-bv dims-ptr
          (%hdf5-check
            (%H5LTget_dataset_info file-id dataset-path dims-ptr (cptr-null) (cptr-null))
            "hdf5-read (get info)"))
        (let* ((shape (let loop ((i 0) (acc '()))
                        (if (= i rank) (reverse acc)
                            (loop (+ i 1) (cons (%u64le-ref dims-bv (* i 8)) acc)))))
               (t (make-tensor shape)))
          (with-pinned-tensor t data-ptr
            (%hdf5-check (%H5LTread_dataset_double file-id dataset-path data-ptr)
                         (string-append "hdf5-read " dataset-path)))
          t)))))

;; ── Public: attributes ───────────────────────────────────────────────────────

(define (hdf5-attributes f object-path)
  (let ((file-id (cdr (assq 'id f))))
    (let loop ((idx 0) (acc '()))
      (let ((attr-id (%H5Aopen_by_idx file-id object-path %H5-INDEX-NAME %H5-ITER-INC
                                       idx %H5P-DEFAULT %H5P-DEFAULT)))
        (if (< attr-id 0)
            (reverse acc)   ; no more attributes at this index
            (let* ((name (%hdf5-read-attr-name attr-id))
                   (val (%hdf5-read-attr-value file-id object-path name attr-id)))
              (%H5Aclose attr-id)
              (loop (+ idx 1) (cons (cons name val) acc))))))))

(define (%hdf5-read-attr-name attr-id)
  (let* ((namebuf (make-bytevector 256 0))
         (len (with-pinned-bytevector namebuf p (%H5Aget_name attr-id 256 p))))
    (%hdf5-check-id len "hdf5-attributes (get name)")
    (let loop ((i 0))
      (if (or (= i 256) (= (bytevector-u8-ref namebuf i) 0))
          (utf8->string (%bv-take namebuf i))
          (loop (+ i 1))))))

(define (%bv-take bv n)
  (let ((out (make-bytevector n 0)))
    (let loop ((i 0)) (when (< i n) (bytevector-u8-set! out i (bytevector-u8-ref bv i)) (loop (+ i 1))))
    out))

;; Read a NUL-terminated string starting at an arbitrary address by peeking
;; growing chunks until a NUL byte turns up. Small over-reads past a short
;; heap string are safe in practice (allocator slack within the same page);
;; the exponential cap keeps a genuinely unterminated buffer from peeking
;; forever into unrelated/unmapped memory.
(define (%hdf5-cstring-at addr)
  (let loop ((size 256))
    (if (> size 65536)
        (error "hdf5: variable-length string attribute has no NUL terminator within 64KB")
        (let* ((buf (peek-bytes (make-cptr addr) size))
               (nul (let scan ((i 0))
                      (cond ((= i size) #f)
                            ((= (bytevector-u8-ref buf i) 0) i)
                            (else (scan (+ i 1)))))))
          (if nul
              (utf8->string (%bv-take buf nul))
              (loop (* size 4)))))))

;; String attributes need care: HDF5 has two on-disk string kinds — fixed-
;; length (H5Aread writes the raw characters directly into a caller buffer —
;; H5Tget_size reports the real string length for this kind) and variable-
;; length, h5py's default (the "stored size" is the size of a pointer, and
;; H5Aread writes an actual pointer to HDF5-owned heap memory — must be
;; dereferenced with peek-bytes, then freed with H5free_memory).
(define (%hdf5-read-attr-value file-id object-path attr-name attr-id)
  (let* ((type-id (%hdf5-check-id (%H5Aget_type attr-id) "hdf5-attributes (get type)"))
         (class (%H5Tget_class type-id)))
    (cond
      ((= class %H5T-STRING)
       (let ((variable? (not (= (%H5Tis_variable_str type-id) 0))))
         (if variable?
             (let* ((ptr-bv (make-bytevector 8 0)))
               (with-pinned-bytevector ptr-bv p
                 (%hdf5-check (%H5Aread attr-id type-id p) "hdf5-attributes (read vlen string)"))
               (let* ((addr (%u64le-ref ptr-bv 0))
                      (str (%hdf5-cstring-at addr)))
                 (%H5free_memory (make-cptr addr))
                 (%H5Tclose type-id)
                 str))
             (let* ((size (%H5Tget_size type-id))
                    (buf (make-bytevector size 0)))
               (with-pinned-bytevector buf p
                 (%hdf5-check (%H5Aread attr-id type-id p) "hdf5-attributes (read fixed string)"))
               (%H5Tclose type-id)
               (let loop ((i 0))
                 (if (or (= i size) (= (bytevector-u8-ref buf i) 0))
                     (utf8->string (%bv-take buf i))
                     (loop (+ i 1))))))))
      ((or (= class %H5T-FLOAT) (= class %H5T-INTEGER))
       ;; H5Aread with mem_type_id = the attribute's own stored type yields
       ;; that exact on-disk representation (whatever its byte width) —
       ;; NOT automatically converted to double. Size the buffer and decode
       ;; per-element using the type's real size and class, rather than
       ;; assuming every numeric attribute is an 8-byte double.
       (let* ((elt-size (%H5Tget_size type-id))
              (space-id (%H5Aget_space attr-id))
              (n (%H5Sget_simple_extent_npoints space-id))
              (buf (make-bytevector (* n elt-size) 0)))
         (%H5Sclose space-id)
         (with-pinned-bytevector buf p (%hdf5-check (%H5Aread attr-id type-id p) "hdf5-attributes (read numeric)"))
         (let ((decode (%hdf5-numeric-decoder class elt-size)))
           (%H5Tclose type-id)
           (let ((vals (let loop ((i 0) (acc '()))
                         (if (= i n) (reverse acc)
                             (loop (+ i 1) (cons (decode buf (* i elt-size)) acc))))))
             (if (= n 1) (car vals) vals)))))
      (else
       (%H5Tclose type-id)
       (error "hdf5: unsupported attribute type (only string/integer/float are read)")))))

;; Pick a native-endian decoder for a numeric attribute based on its real
;; on-disk class and byte width (H5Aread does not implicitly widen to
;; double — the stored type's own size determines the buffer layout).
(define (%hdf5-numeric-decoder class size)
  (cond
    ((and (= class %H5T-FLOAT) (= size 4)) %hdf5-f32-native)
    ((and (= class %H5T-FLOAT) (= size 8)) %hdf5-f64-native)
    ((= class %H5T-INTEGER)
     (lambda (bv i) (%hdf5-sint-native bv i size)))
    (else (error "hdf5: unsupported numeric attribute width" size))))

(define (%hdf5-f64-native bv i) (%bits->f64 (%u64le-ref bv i)))

(define (%hdf5-f32-native bv i)
  (let* ((bits (let loop ((k 3) (acc 0))
                 (if (< k 0) acc
                     (loop (- k 1) (+ (arithmetic-shift acc 8) (bytevector-u8-ref bv (+ i k))))))))
    (%bits->f32 bits)))

;; Native-endian signed integer of 1/2/4/8 bytes (two's complement).
(define (%hdf5-sint-native bv i size)
  (let* ((u (let loop ((k (- size 1)) (acc 0))
              (if (< k 0) acc
                  (loop (- k 1) (+ (arithmetic-shift acc 8) (bytevector-u8-ref bv (+ i k))))))))
    (if (>= u (expt 2 (- (* size 8) 1))) (- u (expt 2 (* size 8))) u)))

;; single precision: 1 sign / 8 exponent (bias 127) / 23 mantissa — same
;; decomposition as (curry private binary-io)'s %bv-f32be, given assembled
;; native-endian bits instead of big-endian bytes.
(define (%bits->f32 bits)
  (let* ((sign (if (zero? (arithmetic-shift bits -31)) 1 -1))
         (expo (bitwise-and (arithmetic-shift bits -23) #xFF))
         (mant (bitwise-and bits #x7FFFFF)))
    (exact->inexact
      (cond
        ((and (= expo 0) (= mant 0)) (* sign 0))
        ((= expo #xFF) (if (= mant 0) (if (= sign 1) (/ 1.0 0.0) (/ -1.0 0.0)) (/ 0.0 0.0)))
        ((= expo 0) (* sign (/ mant (expt 2 23)) (expt 2 -126)))
        (else (* sign (+ 1 (/ mant (expt 2 23))) (expt 2 (- expo 127))))))))

;; Reuse the same bit-decomposition approach as (curry private binary-io)'s
;; %bv-f64be, just reading the 64 bits already assembled native-endian
;; above instead of re-deriving big-endian byte order.
(define (%bits->f64 bits)
  (let* ((sign (if (zero? (arithmetic-shift bits -63)) 1 -1))
         (expo (bitwise-and (arithmetic-shift bits -52) #x7FF))
         (mant (bitwise-and bits #xFFFFFFFFFFFFF)))
    (exact->inexact
      (cond
        ((and (= expo 0) (= mant 0)) (* sign 0))
        ((= expo #x7FF) (if (= mant 0) (if (= sign 1) (/ 1.0 0.0) (/ -1.0 0.0)) (/ 0.0 0.0)))
        ((= expo 0) (* sign (/ mant (expt 2 52)) (expt 2 -1022)))
        (else (* sign (+ 1 (/ mant (expt 2 52))) (expt 2 (- expo 1023))))))))

  )) ;; end begin, define-library
