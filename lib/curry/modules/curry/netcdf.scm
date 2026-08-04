;;; (curry netcdf) — NetCDF classic format reader.
;;;
;;; Pure Scheme, no build step, no external library. Supports NetCDF
;;; classic (CDF-1) and 64-bit-offset (CDF-2) files — the documented,
;;; self-contained big-endian binary format, NOT NetCDF-4 (which is
;;; HDF5-backed; use (curry hdf5) for those) and NOT CDF-5 (64-bit data,
;;; a distinct on-disk layout this reader does not parse).
;;;
;;; Read-only: no writer. Supports both fixed-size and record
;;; (unlimited-dimension) variables. The header is parsed sequentially
;;; straight off the port (it is self-delimiting via its own nelems
;;; fields) so this never buffers variable data into memory.
;;;
;;; API:
;;;   (netcdf-open path)                -> handle
;;;   (netcdf-variable nc name)         -> (values tensor dim-names)
;;;   (netcdf-attributes nc [var-name]) -> alist; global attrs if var-name omitted
;;;   (netcdf-dimensions nc)            -> alist of (name . length); #f length = unlimited
;;;   (netcdf-close nc)

(define-library (curry netcdf)
  (import (curry private binary-io))
  (import (scheme base))
  (export
    netcdf-open netcdf-variable netcdf-attributes netcdf-dimensions netcdf-close)
  (begin

;; nc_type tags
(define %NC_BYTE 1) (define %NC_CHAR 2) (define %NC_SHORT 3)
(define %NC_INT 4)  (define %NC_FLOAT 5) (define %NC_DOUBLE 6)

(define (%nc-type-size type)
  (cond ((= type %NC_BYTE) 1) ((= type %NC_CHAR) 1)
        ((= type %NC_SHORT) 2) ((= type %NC_INT) 4)
        ((= type %NC_FLOAT) 4) ((= type %NC_DOUBLE) 8)
        (else (error "netcdf: unsupported nc_type" type))))

(define (%nc-type-decoder type)
  (cond ((= type %NC_BYTE)
         (lambda (bv i) (let ((u (bytevector-u8-ref bv i))) (if (>= u 128) (- u 256) u))))
        ((= type %NC_CHAR) (lambda (bv i) (bytevector-u8-ref bv i)))
        ((= type %NC_SHORT) %bv-s16be)
        ((= type %NC_INT) %bv-s32be)
        ((= type %NC_FLOAT) %bv-f32be)
        ((= type %NC_DOUBLE) %bv-f64be)
        (else (error "netcdf: unsupported nc_type" type))))

;; A value array (name string, attribute values) is padded to a 4-byte
;; boundary in the file.
(define (%nc-padded-size n) (%pad-to-multiple n 4))

(define (%nc-skip-pad port n)
  (let ((pad (- (%nc-padded-size n) n)))
    (when (> pad 0) (%read-exact-bytes port pad))))

;; ── Sequential header readers (port-based; header is self-delimiting so
;;    no seeking or whole-file buffering is needed to parse it). ────────────

(define (%nc-u32 port) (%bv-u32be (%read-exact-bytes port 4) 0))
(define (%nc-u64 port) (%bv-u64be (%read-exact-bytes port 8) 0))
(define (%nc-s32 port) (%bv-s32be (%read-exact-bytes port 4) 0))

(define (%nc-read-name port)
  (let* ((n (%nc-u32 port))
         (bv (%read-exact-bytes port n)))
    (%nc-skip-pad port n)
    (utf8->string bv)))

;; dim_list / att_list / var_list all share "tag(4) nelems(4)"; tag=0 means
;; absent (and nelems is then always 0 too).
(define (%nc-read-tag-count port) (values (%nc-u32 port) (%nc-u32 port)))

(define (%nc-read-dim-list port)
  (call-with-values (lambda () (%nc-read-tag-count port))
    (lambda (tag n)
      (let loop ((i 0) (acc '()))
        (if (= i n) (reverse acc)
            (let* ((name (%nc-read-name port))
                   (len (%nc-u32 port)))
              (loop (+ i 1) (cons (cons name len) acc))))))))

(define (%nc-read-attr-value port type n)
  (let* ((size (%nc-type-size type))
         (decode (%nc-type-decoder type))
         (nbytes (* n size))
         (raw (%read-exact-bytes port nbytes)))
    (%nc-skip-pad port nbytes)
    (if (= type %NC_CHAR)
        (utf8->string raw)
        (let loop ((i 0) (acc '()))
          (if (= i n) (reverse acc)
              (loop (+ i 1) (cons (decode raw (* i size)) acc)))))))

(define (%nc-read-att-list port)
  (call-with-values (lambda () (%nc-read-tag-count port))
    (lambda (tag n)
      (let loop ((i 0) (acc '()))
        (if (= i n) (reverse acc)
            (let* ((name (%nc-read-name port))
                   (type (%nc-u32 port))
                   (nelems (%nc-u32 port))
                   (val (%nc-read-attr-value port type nelems)))
              (loop (+ i 1) (cons (cons name val) acc))))))))

;; version: 1 = classic (32-bit begin), 2 = 64-bit-offset (64-bit begin)
(define (%nc-read-var-list port version)
  (call-with-values (lambda () (%nc-read-tag-count port))
    (lambda (tag n)
      (let loop ((i 0) (acc '()))
        (if (= i n) (reverse acc)
            (let* ((name (%nc-read-name port))
                   (ndims (%nc-u32 port))
                   (dimids (let dloop ((j 0) (acc '()))
                             (if (= j ndims) (reverse acc)
                                 (dloop (+ j 1) (cons (%nc-u32 port) acc)))))
                   (attrs (%nc-read-att-list port))
                   (type (%nc-u32 port))
                   (vsize (%nc-u32 port))
                   (begin-off (if (= version 1) (%nc-u32 port) (%nc-u64 port))))
              (loop (+ i 1)
                    (cons (list (cons 'name name)
                                (cons 'dimids dimids)
                                (cons 'attrs attrs)
                                (cons 'type type)
                                (cons 'vsize vsize)
                                (cons 'begin begin-off))
                          acc))))))))

;; ── Public: open ─────────────────────────────────────────────────────────────

(define (netcdf-open path)
  (let* ((port (open-input-file path))
         (magic (%read-exact-bytes port 4)))
    (if (not (and (= (bytevector-u8-ref magic 0) #x43)   ; 'C'
                  (= (bytevector-u8-ref magic 1) #x44)   ; 'D'
                  (= (bytevector-u8-ref magic 2) #x46))) ; 'F'
        (error "netcdf: not a NetCDF classic file (bad magic)" path))
    (let ((version (bytevector-u8-ref magic 3)))
      (when (not (or (= version 1) (= version 2)))
        (error "netcdf: unsupported format version (only classic v1 and 64-bit-offset v2 are supported; NetCDF-4/CDF-5 are not)" version))
      (let* ((numrecs (%nc-u32 port))
             (dims (%nc-read-dim-list port))
             (gatts (%nc-read-att-list port))
             (vars (%nc-read-var-list port version)))
        (close-port port)
        (list (cons 'path path)
              (cons 'version version)
              (cons 'numrecs numrecs)
              (cons 'dims dims)
              (cons 'gatts gatts)
              (cons 'vars vars))))))

(define (netcdf-close nc) #t)   ; file already closed after header parse

;; ── Accessors ────────────────────────────────────────────────────────────────

(define (%nc-field nc key) (cdr (assq key nc)))

(define (netcdf-dimensions nc)
  (map (lambda (d) (cons (car d) (if (= (cdr d) 0) #f (cdr d))))
       (%nc-field nc 'dims)))

(define (netcdf-attributes nc . var-name)
  (if (null? var-name)
      (%nc-field nc 'gatts)
      (cdr (assq 'attrs (%nc-find-var nc (car var-name))))))

(define (%nc-find-var nc name)
  (let ((hit (%find (lambda (v) (string=? (cdr (assq 'name v)) name))
                     (%nc-field nc 'vars))))
    (or hit (error "netcdf: no such variable" name))))

(define (%find pred lst)
  (cond ((null? lst) #f)
        ((pred (car lst)) (car lst))
        (else (%find pred (cdr lst)))))

;; ── Public: read a variable's data ───────────────────────────────────────────

(define (netcdf-variable nc name)
  (let* ((v (%nc-find-var nc name))
         (dims (%nc-field nc 'dims))
         (dimids (cdr (assq 'dimids v)))
         (dim-recs (map (lambda (id) (list-ref dims id)) dimids))
         (dim-names (map car dim-recs))
         (record-dim? (and (pair? dim-recs) (= (cdr (car dim-recs)) 0)))
         (type (cdr (assq 'type v)))
         (elt-size (%nc-type-size type))
         (decode (%nc-type-decoder type))
         (numrecs (%nc-field nc 'numrecs))
         (port (open-input-file (%nc-field nc 'path))))
    (if record-dim?
        ;; First dim is the record/unlimited dimension: numrecs records,
        ;; each of size vsize (already padded), spaced recsize apart.
        (let* ((per-record-shape (map cdr (cdr dim-recs)))
               (per-record-count (apply * (if (null? per-record-shape) (list 1) per-record-shape)))
               (recsize (%nc-compute-recsize nc))
               (begin-off (cdr (assq 'begin v)))
               (t (make-tensor (cons numrecs per-record-shape))))
          (let loop ((r 0) (at 0))
            (when (< r numrecs)
              (let* ((rec-off (+ begin-off (* r recsize))))
                (%nc-seek port (- rec-off at))
                (let ((raw (%read-exact-bytes port (* per-record-count elt-size))))
                  (let iloop ((i 0))
                    (when (< i per-record-count)
                      (%nc-tensor-flat-set! t (+ (* r per-record-count) i)
                                            (decode raw (* i elt-size)))
                      (iloop (+ i 1))))
                  (loop (+ r 1) (+ rec-off (* per-record-count elt-size)))))))
          (close-port port)
          (values t (cons "__record__" dim-names)))
        ;; Fixed-size variable: one contiguous block at begin-off.
        (let* ((shape (map cdr dim-recs))
               (count (apply * (if (null? shape) (list 1) shape)))
               (begin-off (cdr (assq 'begin v))))
          (%nc-seek port begin-off)
          (let ((raw (%read-exact-bytes port (* count elt-size)))
                (t (make-tensor shape)))
            (let loop ((i 0))
              (when (< i count)
                (%nc-tensor-flat-set! t i (decode raw (* i elt-size)))
                (loop (+ i 1))))
            (close-port port)
            (values t dim-names))))))

;; recsize = sum of vsize over all variables that use the record dimension
;; (i.e. whose first dim has length 0), per the NetCDF classic spec.
(define (%nc-compute-recsize nc)
  (let ((dims (%nc-field nc 'dims)))
    (fold-left
      (lambda (acc v)
        (let ((dimids (cdr (assq 'dimids v))))
          (if (and (pair? dimids) (= (cdr (list-ref dims (car dimids))) 0))
              (+ acc (cdr (assq 'vsize v)))
              acc)))
      0
      (%nc-field nc 'vars))))

;; No port-seek primitive exists; skip forward by reading and discarding —
;; fine here since each netcdf-variable call opens a fresh port and only
;; skips to its own variable's offset once.
(define (%nc-seek port n)
  (let loop ((remaining n))
    (when (> remaining 0)
      (let ((chunk-size (min remaining 65536)))
        (%read-exact-bytes port chunk-size)
        (loop (- remaining chunk-size))))))

(define (%nc-tensor-flat-set! t flat-i v)
  (let loop ((shape (tensor-shape t)) (rem flat-i) (idx '()))
    (if (null? shape)
        (apply tensor-set! t (append (reverse idx) (list v)))
        (let* ((sub (apply * (if (null? (cdr shape)) (list 1) (cdr shape))))
               (d (if (null? (cdr shape)) rem (quotient rem sub)))
               (r (if (null? (cdr shape)) 0 (remainder rem sub))))
          (loop (cdr shape) r (cons d idx))))))

  )) ;; end begin, define-library
