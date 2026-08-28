(define-library (srfi srfi-18)
  (import (srfi s18 multithreading))
  (import (curry private lang-aliases))
  (export
    current-thread thread? make-thread thread-name thread-start!
    thread-yield! thread-sleep! thread-join! thread-terminate!
    thread-specific thread-specific-set! make-mutex mutex? mutex-lock!
    mutex-unlock! make-condition-variable condition-variable?
    condition-variable-signal! condition-variable-broadcast!
    join-timeout-exception? terminated-thread-exception?
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; ilku: "assigned duty, corvée service" -- a genuine OB term for
    ;; compulsory assigned labor, an apt fit for a scheduled unit of
    ;; independent work. dagiltum (from dagālum, "to watch/wait for") --
    ;; a condition variable is exactly a thing waited upon.
    ilku? 𒉡𒄿𒀭 epēš-ilkim 𒌑𒈷 ilku-annûm 𒊻𒌋𒀭 šumu-ilkim 𒆠𒀀 šurrû-ilkim 𒍪𒁹𒂍 wuššur-ilkim 𒈷𒂗𒅆 ṣalāl-ilkim 𒂗𒃲𒀸 kaṣār-ilkim 𒄀𒁀 qatê-ilkim 𒇲𒁀𒈠 puzri-ilkim 𒋻𒄿𒌋 šakān-puzri-ilkim 𒆠𒁀 dagiltum? 𒀀𒍪 epēš-dagiltim 𒆜𒃲𒆠 šasû-dagiltim 𒀭𒅆 šasû-gabbi-dagiltim 𒇲𒂗𒆠 adan-kaṣārim? 𒆜𒄀 qatê-ilkim-šanûm? 𒂍𒉡𒍪)
  (begin
    (define-name-aliases
      (thread?                        ilku?                    𒉡𒄿𒀭)
      (make-thread                    epēš-ilkim               𒌑𒈷)
      (current-thread                 ilku-annûm               𒊻𒌋𒀭)
      (thread-name                    šumu-ilkim               𒆠𒀀)
      (thread-start!                  šurrû-ilkim              𒍪𒁹𒂍)
      (thread-yield!                  wuššur-ilkim             𒈷𒂗𒅆)
      (thread-sleep!                  ṣalāl-ilkim              𒂗𒃲𒀸)
      (thread-join!                   kaṣār-ilkim              𒄀𒁀)
      (thread-terminate!              qatê-ilkim               𒇲𒁀𒈠)
      (thread-specific                puzri-ilkim              𒋻𒄿𒌋)
      (thread-specific-set!           šakān-puzri-ilkim        𒆠𒁀)
      (condition-variable?            dagiltum?                𒀀𒍪)
      (make-condition-variable        epēš-dagiltim            𒆜𒃲𒆠)
      (condition-variable-signal!     šasû-dagiltim            𒀭𒅆)
      (condition-variable-broadcast!  šasû-gabbi-dagiltim      𒇲𒂗𒆠)
      (join-timeout-exception?        adan-kaṣārim?            𒆜𒄀)
      (terminated-thread-exception?   qatê-ilkim-šanûm?        𒂍𒉡𒍪))))
