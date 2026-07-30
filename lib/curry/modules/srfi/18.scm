(define-library (srfi 18)
  (import (srfi s18 multithreading))
  (export
    current-thread thread? make-thread thread-name thread-start!
    thread-yield! thread-sleep! thread-join! thread-terminate!
    thread-specific thread-specific-set! make-mutex mutex? mutex-lock!
    mutex-unlock! make-condition-variable condition-variable?
    condition-variable-signal! condition-variable-broadcast!
    join-timeout-exception? terminated-thread-exception?))
