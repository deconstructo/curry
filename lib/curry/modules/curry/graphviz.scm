;;; (curry graphviz) — DOT graph builder and writer, pure Scheme.
;;;
;;; A small mutable graph object (make-graph) plus add-node!/add-edge!/
;;; add-subgraph! builders, so callers never hand-format DOT syntax
;;; themselves — the same "raw format + ergonomic builder API" shape
;;; (curry toml)/(curry yaml) already use, applied to a write-only format
;;; instead of a read/write one (DOT has no reader here; Graphviz's own
;;; `dot` doesn't round-trip through this module, and nothing in the
;;; Scheme ecosystem needs it to).
;;;
;;; Node/edge identifiers and attribute values are auto-quoted (always
;;; quoted unless the value is a number) rather than trying to detect
;;; every case DOT's own "bare identifier" grammar permits unquoted —
;;; simpler, and a quoted string is always valid DOT regardless of its
;;; content, so there's nothing to gain from the extra complexity of a
;;; bare-identifier fast path here.
;;;
;;; `graph-render!` renders straight to an image via libgvc/libcgraph,
;;; dlopen'd at runtime (not linked at build time — no CMake flag beyond
;;; the general `BUILD_FFI=ON`), the same pattern `(curry hdf5)`/`(curry
;;; ncurses)` use for their own native libraries. It's the only part of
;;; this module with an external dependency (Graphviz's C libraries must
;;; be installed) — `graph->dot-string`/`graph-write-dot` need nothing
;;; beyond this module. Install: `brew install graphviz` (macOS),
;;; `apt install libgraphviz-dev` (Debian/Ubuntu), `dnf install
;;; graphviz-devel` (Fedora/RHEL).

(define-library (curry graphviz)
  (import (scheme base) (scheme write) (curry ffi))
  (export
    make-graph graph? graph-directed? graph-name
    graph-add-node! graph-add-edge! graph-add-subgraph!
    graph-set-attr! graph-set-node-defaults! graph-set-edge-defaults!
    graph->dot-string graph-write-dot graph-render!)
  (begin

;;; =========================================================================
;;; The graph object
;;; =========================================================================

(define-record-type <graph>
  (%make-graph name directed? cluster? attrs nodes edges subgraphs node-defaults edge-defaults)
  graph?
  (name          graph-name)
  (directed?     graph-directed?)
  (cluster?      graph-cluster?)
  (attrs         graph-attrs         set-graph-attrs!)
  (nodes         graph-nodes         set-graph-nodes!)
  (edges         graph-edges         set-graph-edges!)
  (subgraphs     graph-subgraphs     set-graph-subgraphs!)
  (node-defaults graph-node-defaults set-graph-node-defaults!)
  (edge-defaults graph-edge-defaults set-graph-edge-defaults!))

;; (make-graph name)            -> a directed graph named `name`
;; (make-graph name directed?)  -> directed? #f for an undirected graph
(define (make-graph name . opts)
  (let ((directed? (if (pair? opts) (car opts) #t)))
    (%make-graph name directed? #f '() '() '() '() '() '())))

;; (graph-add-node! g id)            -> adds a node with no attributes
;; (graph-add-node! g id attrs)      -> attrs is an alist, e.g. '((color . red))
(define (graph-add-node! g id . opts)
  (set-graph-nodes! g (append (graph-nodes g) (list (cons id (if (pair? opts) (car opts) '()))))))

;; (graph-add-edge! g from to)       -> adds an edge with no attributes
;; (graph-add-edge! g from to attrs)
(define (graph-add-edge! g from to . opts)
  (set-graph-edges! g (append (graph-edges g) (list (list from to (if (pair? opts) (car opts) '()))))))

;; (graph-add-subgraph! g name)            -> a cluster subgraph (bordered
;;                                            box when rendered) named `name`
;; (graph-add-subgraph! g name cluster?)   -> cluster? #f for a plain,
;;                                            border-less subgraph
;; Returns the new subgraph, itself a <graph> that graph-add-node!/
;; graph-add-edge!/graph-add-subgraph! all work on directly.
(define (graph-add-subgraph! g name . opts)
  (let* ((cluster? (if (pair? opts) (car opts) #t))
         (sub (%make-graph name (graph-directed? g) cluster? '() '() '() '() '() '())))
    (set-graph-subgraphs! g (append (graph-subgraphs g) (list sub)))
    sub))

;; Overwrites `key`'s value if already set, same "last write wins,
;; insertion order preserved otherwise" convention (curry toml)'s own
;; mutable parse-time table uses.
(define (%alist-upsert alist key value)
  (let ((p (assoc key alist)))
    (if p (begin (set-cdr! p value) alist) (append alist (list (cons key value))))))

;; A graph-level attribute line, e.g. (graph-set-attr! g 'rankdir "LR").
(define (graph-set-attr! g key value) (set-graph-attrs! g (%alist-upsert (graph-attrs g) key value)))

;; Default attributes applied to every node/edge added to `g` from this
;; point in the DOT source onward (Graphviz's own `node [...]`/`edge
;; [...]` statement semantics — this module doesn't retroactively apply
;; them to nodes/edges already added, matching Graphviz itself).
(define (graph-set-node-defaults! g attrs) (set-graph-node-defaults! g attrs))
(define (graph-set-edge-defaults! g attrs) (set-graph-edge-defaults! g attrs))

;;; =========================================================================
;;; Writing
;;; =========================================================================

(define (%stringify v)
  (cond ((string? v) v) ((symbol? v) (symbol->string v)) ((number? v) (number->string v))
        ((char? v) (string v))
        (else (let ((out (open-output-string))) (write v out) (get-output-string out)))))

;; Graphviz's own convention for turning a subgraph into a visibly
;; bordered cluster: its name must start with "cluster". Prepends
;; "cluster_" for a caller who set cluster? without naming it that way
;; themselves.
(define (%cluster-name name)
  (let ((s (%stringify name)))
    (if (and (>= (string-length s) 7) (string=? (substring s 0 7) "cluster"))
        s
        (string-append "cluster_" s))))

(define (%write-quoted s out)
  (display "\"" out)
  (string-for-each
    (lambda (c)
      (case c
        ((#\") (display "\\\"" out))
        ((#\\) (display "\\\\" out))
        ((#\newline) (display "\\n" out))
        (else (display c out))))
    s)
  (display "\"" out))

;; A DOT identifier or attribute value: numbers are written bare
;; (Graphviz treats them specially for e.g. numeric comparisons in some
;; contexts), everything else is always quoted -- see the module header
;; comment for why this doesn't try to detect DOT's bare-identifier cases.
(define (%write-id v out) (if (number? v) (display v out) (%write-quoted (%stringify v) out)))

(define (%write-attrs attrs out)
  (unless (null? attrs)
    (display " [" out)
    (let loop ((as attrs) (first #t))
      (unless (null? as)
        (unless first (display ", " out))
        (display (%stringify (car (car as))) out)
        (display "=" out)
        (%write-id (cdr (car as)) out)
        (loop (cdr as) #f)))
    (display "]" out)))

(define (%write-graph g out indent top?)
  (if top?
      (begin
        (display (if (graph-directed? g) "digraph " "graph ") out)
        (%write-id (or (graph-name g) 'G) out)
        (display " {\n" out))
      (begin
        (display indent out)
        (display "subgraph " out)
        (%write-id (if (graph-cluster? g) (%cluster-name (graph-name g)) (graph-name g)) out)
        (display " {\n" out)))
  (let ((inner (string-append indent "  ")))
    (for-each
      (lambda (kv)
        (display inner out) (display (%stringify (car kv)) out) (display "=" out)
        (%write-id (cdr kv) out) (display ";\n" out))
      (graph-attrs g))
    (unless (null? (graph-node-defaults g))
      (display inner out) (display "node" out) (%write-attrs (graph-node-defaults g) out) (display ";\n" out))
    (unless (null? (graph-edge-defaults g))
      (display inner out) (display "edge" out) (%write-attrs (graph-edge-defaults g) out) (display ";\n" out))
    (for-each (lambda (sub) (%write-graph sub out inner #f)) (graph-subgraphs g))
    (for-each
      (lambda (nd) (display inner out) (%write-id (car nd) out) (%write-attrs (cdr nd) out) (display ";\n" out))
      (graph-nodes g))
    (for-each
      (lambda (ed)
        (display inner out)
        (%write-id (car ed) out) (display (if (graph-directed? g) " -> " " -- ") out) (%write-id (cadr ed) out)
        (%write-attrs (caddr ed) out) (display ";\n" out))
      (graph-edges g)))
  (display indent out) (display "}\n" out))

;; (graph-write-dot g port) -> writes `g` as DOT source directly to `port`.
(define (graph-write-dot g port) (%write-graph g port "" #t))

;; (graph->dot-string g) -> `g` as a DOT source string.
(define (graph->dot-string g)
  (let ((out (open-output-string)))
    (graph-write-dot g out)
    (get-output-string out)))

;;; =========================================================================
;;; Rendering (libgvc + libcgraph, dlopen'd at runtime)
;;; =========================================================================
;;;
;;; Both libraries' shared-library naming/location is inconsistent across
;;; platforms; probe a candidate list per library and use the first one
;;; that dlopens, rather than assuming a single hardcoded path — same
;;; approach (curry hdf5)/(curry ncurses) use for their own native libs.

(define %gvc-candidates
  (list
    "libgvc.dylib"                                       ; macOS, on loader path
    "libgvc.so"                                          ; Linux, on loader path
    "/opt/homebrew/lib/libgvc.dylib"                      ; Homebrew, Apple Silicon
    "/usr/local/lib/libgvc.dylib"                         ; Homebrew, Intel Mac
    "/usr/lib/x86_64-linux-gnu/libgvc.so.6"               ; Debian/Ubuntu, x86_64
    "/usr/lib/aarch64-linux-gnu/libgvc.so.6"              ; Debian/Ubuntu, arm64
    "/usr/lib64/libgvc.so.6"))                            ; Fedora/RHEL

(define %cgraph-candidates
  (list
    "libcgraph.dylib"
    "libcgraph.so"
    "/opt/homebrew/lib/libcgraph.dylib"
    "/usr/local/lib/libcgraph.dylib"
    "/usr/lib/x86_64-linux-gnu/libcgraph.so.6"
    "/usr/lib/aarch64-linux-gnu/libcgraph.so.6"
    "/usr/lib64/libcgraph.so.6"))

(define (%gv-try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

;; Both libraries are loaded lazily (only on the first graph-render! call,
;; not at import time) so importing (curry graphviz) never requires
;; Graphviz to be installed -- only actually rendering does. graph->dot-
;; string/graph-write-dot/the whole builder API above work with nothing
;; beyond this module, exactly as the header comment says.
(define %gvc-lib #f)
(define %cgraph-lib #f)

(define (%gv-ensure-libs!)
  (unless %gvc-lib
    (set! %gvc-lib
      (or (%gv-try-load %gvc-candidates)
          (error "graphviz: could not load libgvc — install Graphviz first:
  macOS:           brew install graphviz
  Debian/Ubuntu:   apt install libgraphviz-dev
  Fedora/RHEL:     dnf install graphviz-devel"))))
  (unless %cgraph-lib
    (set! %cgraph-lib
      (or (%gv-try-load %cgraph-candidates)
          (error "graphviz: found libgvc but not libcgraph — it usually ships in the same package; check your installation")))))

;; Raw foreign bindings, bound lazily (define-foreign's #:from library
;; argument is evaluated once, at define-foreign's own definition time,
;; so these can't simply reference %gvc-lib/%cgraph-lib before %gv-
;; ensure-libs! has populated them) -- wrapped in a thunk each, called
;; only from %gv-render-raw!, itself only reachable through graph-
;; render! after %gv-ensure-libs! has already run.
(define (%gv-bind!)
  (set! %agmemread
    (let ((fn (%ffi-make-fn %cgraph-lib "agmemread" 'c-ptr '(c-string))))
      (lambda (cp) (%ffi-call fn (list cp)))))
  (set! %agclose
    (let ((fn (%ffi-make-fn %cgraph-lib "agclose" 'int '(c-ptr))))
      (lambda (g) (%ffi-call fn (list g)))))
  (set! %gvContext
    (let ((fn (%ffi-make-fn %gvc-lib "gvContext" 'c-ptr '())))
      (lambda () (%ffi-call fn '()))))
  (set! %gvFreeContext
    (let ((fn (%ffi-make-fn %gvc-lib "gvFreeContext" 'int '(c-ptr))))
      (lambda (gvc) (%ffi-call fn (list gvc)))))
  (set! %gvLayout
    (let ((fn (%ffi-make-fn %gvc-lib "gvLayout" 'int '(c-ptr c-ptr c-string))))
      (lambda (gvc g engine) (%ffi-call fn (list gvc g engine)))))
  (set! %gvFreeLayout
    (let ((fn (%ffi-make-fn %gvc-lib "gvFreeLayout" 'int '(c-ptr c-ptr))))
      (lambda (gvc g) (%ffi-call fn (list gvc g)))))
  (set! %gvRenderFilename
    (let ((fn (%ffi-make-fn %gvc-lib "gvRenderFilename" 'int '(c-ptr c-ptr c-string c-string))))
      (lambda (gvc g format filename) (%ffi-call fn (list gvc g format filename))))))

(define %agmemread #f) (define %agclose #f)
(define %gvContext #f) (define %gvFreeContext #f)
(define %gvLayout #f) (define %gvFreeLayout #f) (define %gvRenderFilename #f)
(define %gv-bound? #f)

;; (graph-render! g out-path)                -> renders to a PNG at out-path
;; (graph-render! g out-path format)          -> format is any Graphviz
;;                                               output-format name ("png",
;;                                               "svg", "pdf", ...)
;; (graph-render! g out-path format engine)    -> layout engine ("dot",
;;                                               "neato", "fdp", "circo",
;;                                               "twopi", ... — default "dot")
;;
;; Parses this module's own generated DOT text via agmemread (so building
;; the layout engine's own graph-construction API isn't needed at all —
;; graph->dot-string already does the one job that would otherwise
;; duplicate), lays it out, renders straight to `out-path`, and frees
;; every native handle before returning (or before raising, on any
;; failure partway through). Raises if Graphviz's libraries can't be
;; found, or if any of agmemread/gvLayout/gvRenderFilename fails.
(define (graph-render! g out-path . opts)
  (let* ((format (if (pair? opts) (car opts) "png"))
         (opts   (if (pair? opts) (cdr opts) '()))
         (engine (if (pair? opts) (car opts) "dot")))
    (%gv-ensure-libs!)
    (unless %gv-bound? (%gv-bind!) (set! %gv-bound? #t))
    (let ((gvc (%gvContext)))
      (when (cptr-null? gvc) (error "graphviz: gvContext() returned NULL"))
      (let ((ag (%agmemread (graph->dot-string g))))
        (when (cptr-null? ag)
          (%gvFreeContext gvc)
          (error "graphviz: agmemread() failed to parse the generated DOT source"))
        (let ((layout-rc (%gvLayout gvc ag engine)))
          (if (not (zero? layout-rc))
              (begin (%agclose ag) (%gvFreeContext gvc)
                     (error "graphviz: gvLayout failed" layout-rc engine))
              (let ((render-rc (%gvRenderFilename gvc ag format out-path)))
                (%gvFreeLayout gvc ag)
                (%agclose ag)
                (%gvFreeContext gvc)
                (unless (zero? render-rc)
                  (error "graphviz: gvRenderFilename failed" render-rc format))
                render-rc)))))))

  )) ;; end begin, define-library
