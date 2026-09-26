(define-library (srfi srfi-29)
  (import (srfi s29 localization))
  (export
    current-language current-country current-locale-details
    declare-bundle! store-bundle! load-bundle!
    localized-template))
