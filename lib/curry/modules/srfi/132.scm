(define-library (srfi 132)
  (import (srfi s132 sorting))
  (export
    list-sorted? vector-sorted? list-sort list-stable-sort list-sort!
    list-stable-sort! vector-sort vector-sort! vector-stable-sort
    vector-stable-sort! list-merge list-merge! vector-merge vector-merge!))
