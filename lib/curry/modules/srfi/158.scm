(define-library (srfi 158)
  (import (srfi s158 generators-and-accumulators))
  (export
    generator make-coroutine-generator make-for-each-generator
    list->generator vector->generator string->generator make-range-generator
    make-iota-generator circular-generator generator->list generator->vector
    generator->string gtake gdrop gappend gcons* gmap gfilter gremove gzip
    gflatten generator-map->list generator-fold generator-for-each
    generator-each generator-count generator-any generator-every
    generator-find generator-length make-accumulator count-accumulator
    list-accumulator reverse-list-accumulator vector-accumulator
    sum-accumulator product-accumulator))
