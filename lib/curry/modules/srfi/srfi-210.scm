(define-library (srfi srfi-210)
  (import (srfi s210 multiple-values))
  (export
    apply/mv call/mv list/mv vector/mv box/mv value/mv coarity
    set!-values with-values case-receive bind/mv
    list-values vector-values box-values value identity
    compose-left compose-right map-values
    bind/list bind/box bind
    %value-mv-ref %sv-assign %cr-dispatch %cr-arity-matches? %chain-transducers
    %call-mv-run))
