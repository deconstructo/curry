# Module: (curry vecdb)

In-memory vector database for nearest-neighbour search. Useful for semantic similarity, embedding retrieval, and clustering. Uses brute-force search by default; can use the `usearch` library for approximate nearest neighbours if available.

## Installation

No required dependencies (brute-force fallback is built-in).

For faster approximate search on large databases, install `usearch`:

```bash
# Build from source or use the header-only version
# https://github.com/unum-cloud/usearch
```

Enable: `-DBUILD_MODULE_VECDB=ON` (off by default).

## Import

```scheme
(import (curry vecdb))
```

## Procedures

```scheme
(vecdb-make dimensions)
(vecdb-make dimensions metric)
```

Create a new vector database. `dimensions` is the number of floats per vector. `metric` (optional symbol) selects the distance function:

| Symbol | Metric | Use case |
|--------|--------|----------|
| `'cosine` | Cosine distance (default) | Semantic similarity, text embeddings |
| `'l2` | Euclidean distance | Spatial search, image features |
| `'ip` | Inner product (negated) | Maximum inner product search |

```scheme
(vecdb-add db id vector)
```

Insert or replace a vector. `id` is a fixnum identifier. `vector` is a Scheme vector of flonums with `dimensions` elements.

```scheme
(vecdb-search db query k)
```

Find the `k` nearest neighbours of `query` (a vector of flonums). Returns a list of `(id . distance)` pairs, sorted by distance (nearest first).

```scheme
(vecdb-remove db id)    ; remove entry by id
(vecdb-size   db)       ; → integer (number of entries)
```

## Examples

### Basic similarity search

```scheme
(import (curry vecdb))

(define db (vecdb-make 4 'cosine))

(vecdb-add db 0 #(1.0  0.0  0.0  0.0))
(vecdb-add db 1 #(0.9  0.1  0.0  0.0))
(vecdb-add db 2 #(0.0  1.0  0.0  0.0))
(vecdb-add db 3 #(0.0  0.0  1.0  0.0))

(vecdb-search db #(1.0 0.05 0.0 0.0) 2)
; => ((0 . 0.0025...) (1 . 0.0050...))   nearest first
```

### Text embedding retrieval

```scheme
(import (curry vecdb))
(import (curry graphql))   ; or any embedding API

; Assume embed returns a vector of flonums
(define (embed text)
  ; Call your embedding API here
  ...)

(define db (vecdb-make 384 'cosine))

(define documents
  '((0 . "The cat sat on the mat.")
    (1 . "A dog chased a ball.")
    (2 . "Feline behaviour is enigmatic.")
    (3 . "Quantum gravity in 2.7 dimensions.")))

(for-each
  (lambda (doc)
    (vecdb-add db (car doc) (embed (cdr doc))))
  documents)

(define query-vec (embed "What do cats do?"))
(define hits (vecdb-search db query-vec 2))

(for-each
  (lambda (hit)
    (display (cdr (assv (car hit) documents)))
    (display " (distance: ") (display (cdr hit)) (display ")")
    (newline))
  hits)
; => "The cat sat on the mat. (distance: 0.04...)"
;    "Feline behaviour is enigmatic. (distance: 0.12...)"
```

### Simulation trajectory clustering

```scheme
(import (curry vecdb))

; Store simulation states as vectors for later retrieval
(define state-db (vecdb-make 6 'l2))  ; (x y z vx vy vz)

(define (record-state! id x y z vx vy vz)
  (vecdb-add state-db id (vector x y z vx vy vz)))

; Find states most similar to a query state
(define (find-similar-states query-state k)
  (vecdb-search state-db query-state k))
```

## Notes

- Vectors must contain only flonums (inexact reals). Use `exact->inexact` on fixnums: `(vector-map exact->inexact v)`.
- Brute-force search is O(n·d) per query (n = database size, d = dimensions). For databases over ~100k entries, consider the `usearch` backend.
- All data is in memory; the database does not persist across restarts. Serialise by iterating entries and writing to a file.
- Distance 0.0 means identical vectors. For cosine distance, 0.0 is perfectly aligned, 1.0 is orthogonal, 2.0 is opposite.
- `id` values are fixnums — use them as indices into your own document store.
