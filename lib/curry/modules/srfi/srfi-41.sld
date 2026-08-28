(define-library (srfi srfi-41)
  (import (srfi s41 streams))
  (export
    stream-null stream-cons stream? stream-null? stream-pair?
    stream-car stream-cdr stream-lambda
    define-stream list->stream stream stream->list
    stream-filter stream-map stream-from stream-take
    stream-append stream-concat stream-constant
    stream-drop stream-drop-while stream-fold stream-for-each
    stream-iterate stream-length stream-of stream-of-aux
    stream-range stream-ref stream-reverse stream-scan
    stream-take-while stream-unfold stream-unfolds stream-zip
    stream-let port->stream stream-match
    stream-match-test stream-match-pattern
    %make-stream-pare))
