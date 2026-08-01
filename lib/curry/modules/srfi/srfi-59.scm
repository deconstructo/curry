(define-library (srfi srfi-59)
  (import (srfi s59 vicinity))
  (import (curry private lang-aliases))
  (export
    program-vicinity library-vicinity implementation-vicinity user-vicinity
    home-vicinity in-vicinity sub-vicinity make-vicinity pathname->vicinity
    ->vicinity vicinity:suffix? ->namestring
    ;; Akkadian synonyms -- ašrum: "place, location" (reused from the
    ;; condition system's condition-backtrace), urḫum: "road, path".
    ašru-ana-ṭuppim 𒀀𒃲𒊕 ṭuppu-ana-ašrim 𒄿𒈧𒂗 ašru-bītim 𒁹𒆜𒁀 ašru-epēšim 𒀀𒁀 ina-ašrim 𒁹𒈠 ašru-ṭuppi 𒍪𒂗 epēš-ašrim 𒉡𒈧 urḫu-ana-ašrim 𒂗𒃲𒀀 ašru-šipirtim 𒊻𒀀𒌑 ašru-qerbûm 𒊻𒆜𒆠 ašru-amēlim 𒌋𒊻 zibbat-ašrim? 𒄀𒁹𒇲)
  (begin
    (define-name-aliases
      (->namestring          ašru-ana-ṭuppim      𒀀𒃲𒊕)
      (->vicinity             ṭuppu-ana-ašrim      𒄿𒈧𒂗)
      (home-vicinity          ašru-bītim           𒁹𒆜𒁀)
      (implementation-vicinity ašru-epēšim         𒀀𒁀)
      (in-vicinity            ina-ašrim            𒁹𒈠)
      (library-vicinity       ašru-ṭuppi           𒍪𒂗)
      (make-vicinity          epēš-ašrim           𒉡𒈧)
      (pathname->vicinity     urḫu-ana-ašrim       𒂗𒃲𒀀)
      (program-vicinity       ašru-šipirtim        𒊻𒀀𒌑)
      (sub-vicinity           ašru-qerbûm          𒊻𒆜𒆠)
      (user-vicinity          ašru-amēlim          𒌋𒊻)
      (vicinity:suffix?       zibbat-ašrim?        𒄀𒁹𒇲))))
