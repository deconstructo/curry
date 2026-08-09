;;; Graphviz module tests — (curry graphviz)

(import (curry graphviz))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

;;; Basic construction

(check "empty directed graph"
  (let ((g (make-graph 'G))) (graph->dot-string g))
  "digraph \"G\" {\n}\n")

(check "empty undirected graph"
  (let ((g (make-graph 'G #f))) (graph->dot-string g))
  "graph \"G\" {\n}\n")

(check "graph? predicate"
  (graph? (make-graph 'G))
  #t)

(check "graph-directed? reflects constructor arg"
  (list (graph-directed? (make-graph 'G)) (graph-directed? (make-graph 'G #f)))
  (list #t #f))

;;; Nodes and edges

(check "node with no attrs"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\";\n}\n")

(check "node with attrs"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a '((color . "red"))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" [color=\"red\"];\n}\n")

(check "directed edge uses ->"
  (let ((g (make-graph 'G))) (graph-add-edge! g 'a 'b) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" -> \"b\";\n}\n")

(check "undirected edge uses --"
  (let ((g (make-graph 'G #f))) (graph-add-edge! g 'a 'b) (graph->dot-string g))
  "graph \"G\" {\n  \"a\" -- \"b\";\n}\n")

(check "edge with attrs"
  (let ((g (make-graph 'G))) (graph-add-edge! g 'a 'b '((label . "e1"))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" -> \"b\" [label=\"e1\"];\n}\n")

(check "multiple attrs joined with comma"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a '((color . "red") (shape . "box"))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" [color=\"red\", shape=\"box\"];\n}\n")

(check "numeric attribute value is not quoted"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a '((weight . 5))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" [weight=5];\n}\n")

(check "nodes and edges preserve insertion order"
  (let ((g (make-graph 'G)))
    (graph-add-node! g 'b) (graph-add-node! g 'a)
    (graph-add-edge! g 'b 'a) (graph-add-edge! g 'a 'b)
    (graph->dot-string g))
  "digraph \"G\" {\n  \"b\";\n  \"a\";\n  \"b\" -> \"a\";\n  \"a\" -> \"b\";\n}\n")

;;; Graph attributes and defaults

(check "graph-set-attr! writes a top-level attribute line"
  (let ((g (make-graph 'G))) (graph-set-attr! g 'rankdir "LR") (graph->dot-string g))
  "digraph \"G\" {\n  rankdir=\"LR\";\n}\n")

(check "graph-set-attr! overwrites, doesn't duplicate"
  (let ((g (make-graph 'G))) (graph-set-attr! g 'rankdir "LR") (graph-set-attr! g 'rankdir "TB") (graph->dot-string g))
  "digraph \"G\" {\n  rankdir=\"TB\";\n}\n")

(check "graph-set-node-defaults! writes a node [...] line before nodes"
  (let ((g (make-graph 'G)))
    (graph-set-node-defaults! g '((shape . "box")))
    (graph-add-node! g 'a)
    (graph->dot-string g))
  "digraph \"G\" {\n  node [shape=\"box\"];\n  \"a\";\n}\n")

(check "graph-set-edge-defaults! writes an edge [...] line before edges"
  (let ((g (make-graph 'G)))
    (graph-set-edge-defaults! g '((color . "gray")))
    (graph-add-edge! g 'a 'b)
    (graph->dot-string g))
  "digraph \"G\" {\n  edge [color=\"gray\"];\n  \"a\" -> \"b\";\n}\n")

;; Regression: %stringify's fallback branch (for a value that's not a
;; string/symbol/number/char) used to call call-with-output-string,
;; which doesn't exist in curry -- any boolean/list/vector/pair id or
;; attribute value crashed the whole process with unbound-variable
;; instead of stringifying via write like every other non-primitive
;; value already does elsewhere in this module.
(check "a boolean node id doesn't crash, stringifies via write"
  (let ((g (make-graph 'G))) (graph-add-node! g #t) (graph->dot-string g))
  "digraph \"G\" {\n  \"#t\";\n}\n")

(check "an attribute value that's a list doesn't crash, stringifies via write"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a '((label . ()))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" [label=\"()\"];\n}\n")

;;; Subgraphs / clusters

(check "subgraph defaults to a cluster, name auto-prefixed"
  (let ((g (make-graph 'G)))
    (let ((sub (graph-add-subgraph! g "group1")))
      (graph-add-node! sub 'a))
    (graph->dot-string g))
  "digraph \"G\" {\n  subgraph \"cluster_group1\" {\n    \"a\";\n  }\n}\n")

(check "subgraph name already starting with cluster is left as-is"
  (let ((g (make-graph 'G)))
    (let ((sub (graph-add-subgraph! g "cluster_x")))
      (graph-add-node! sub 'a))
    (graph->dot-string g))
  "digraph \"G\" {\n  subgraph \"cluster_x\" {\n    \"a\";\n  }\n}\n")

(check "non-cluster subgraph name is not prefixed"
  (let ((g (make-graph 'G)))
    (let ((sub (graph-add-subgraph! g "plain" #f)))
      (graph-add-node! sub 'a))
    (graph->dot-string g))
  "digraph \"G\" {\n  subgraph \"plain\" {\n    \"a\";\n  }\n}\n")

(check "nested subgraphs indent correctly"
  (let ((g (make-graph 'G)))
    (let ((sub (graph-add-subgraph! g "outer")))
      (let ((sub2 (graph-add-subgraph! sub "inner")))
        (graph-add-node! sub2 'a)))
    (graph->dot-string g))
  "digraph \"G\" {\n  subgraph \"cluster_outer\" {\n    subgraph \"cluster_inner\" {\n      \"a\";\n    }\n  }\n}\n")

;;; Escaping

(check "label with embedded quote and backslash is escaped"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a `((label . ,(string-append "a\"b\\c")))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" [label=\"a\\\"b\\\\c\"];\n}\n")

(check "label with embedded newline is escaped"
  (let ((g (make-graph 'G))) (graph-add-node! g 'a `((label . ,(string-append "a" (string #\newline) "b")))) (graph->dot-string g))
  "digraph \"G\" {\n  \"a\" [label=\"a\\nb\"];\n}\n")

(check "string node id is quoted like any other"
  (let ((g (make-graph 'G))) (graph-add-node! g "a node") (graph->dot-string g))
  "digraph \"G\" {\n  \"a node\";\n}\n")

;;; Writing directly to a port

(check "graph-write-dot writes the same thing graph->dot-string returns"
  (let ((g (make-graph 'G)) (out (open-output-string)))
    (graph-add-node! g 'a)
    (graph-write-dot g out)
    (get-output-string out))
  (let ((g (make-graph 'G))) (graph-add-node! g 'a) (graph->dot-string g)))

;;; graph-render! — needs Graphviz's own C libraries (libgvc/libcgraph),
;;; dlopen'd lazily on first use, not linked at build time; unlike this
;;; file's other checks, importing (curry graphviz) always succeeds
;;; regardless of whether Graphviz is installed (only render! itself
;;; needs it) -- so guard just this one check rather than the whole file.

(define graphviz-libs-available
  (guard (e (#t #f))
    (let ((g (make-graph 'a)))
      (graph-add-node! g 'x)
      (graph-render! g "/tmp/curry-graphviz-probe.png"))
    #t))

(if (not graphviz-libs-available)
    (begin
      (display "SKIP: libgvc/libcgraph not found on this system — install Graphviz to run graph-render! checks")
      (newline)
      (display "  macOS:         brew install graphviz") (newline)
      (display "  Debian/Ubuntu: apt install libgraphviz-dev") (newline)
      (display "  Fedora/RHEL:   dnf install graphviz-devel") (newline))
    (begin
      (check "graph-render! produces a file"
        (let ((path "/tmp/curry-graphviz-test-output.png"))
          (let ((g (make-graph 'G)))
            (graph-add-node! g 'a) (graph-add-node! g 'b) (graph-add-edge! g 'a 'b)
            (graph-render! g path))
          (call-with-port (open-input-file path)
            (lambda (p) (list (read-char p) (read-char p) (read-char p) (read-char p)))))
        ;; PNG magic bytes: \x89 P N G
        (list (integer->char 137) #\P #\N #\G))
      (check "graph-render! supports non-default formats"
        (let ((path "/tmp/curry-graphviz-test-output.svg"))
          (let ((g (make-graph 'G)))
            (graph-add-node! g 'a)
            (graph-render! g path "svg"))
          (call-with-port (open-input-file path)
            (lambda (p) (read-line p))))
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>")
      (check-error "graph-render! raises on an unknown layout engine name"
        (lambda ()
          (let ((g (make-graph 'G)))
            (graph-add-node! g 'a)
            (graph-render! g "/tmp/curry-graphviz-test-badengine.png" "png" "definitely-not-a-real-engine-xyz"))))))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
