(define-library (srfi 263)
  (import (srfi s263 prototype-objects))
  (export
    *the-root-object*
    slot? slot-getter slot-setter slot-type
    define-method define-object derive-object copy-object))
