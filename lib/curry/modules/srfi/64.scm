(define-library (srfi 64)
  (import (srfi s64 testing))
  (import (curry private lang-aliases))
  (export
    test-assert test-eqv test-eq test-equal test-approximate test-error
    test-read-eval-string test-begin test-end test-group
    test-group-with-cleanup test-skip test-expect-fail test-match-name
    test-match-nth test-match-any test-match-all test-runner?
    test-runner-current test-runner-get test-runner-create test-runner-null
    test-runner-simple test-runner-factory test-apply test-with-runner
    test-result-kind test-passed? test-result-ref test-result-set!
    test-result-remove test-result-clear test-result-alist
    test-runner-pass-count test-runner-fail-count test-runner-xpass-count
    test-runner-xfail-count test-runner-skip-count test-runner-test-name
    test-runner-group-path test-runner-group-stack test-runner-aux-value
    test-runner-aux-value! test-runner-reset test-runner-on-test-begin
    test-runner-on-test-begin! test-runner-on-test-end
    test-runner-on-test-end! test-runner-on-group-begin
    test-runner-on-group-begin! test-runner-on-group-end
    test-runner-on-group-end! test-runner-on-bad-count
    test-runner-on-bad-count! test-runner-on-bad-end-name
    test-runner-on-bad-end-name! test-runner-on-final test-runner-on-final!
    test-on-test-begin-simple test-on-test-end-simple
    test-on-group-begin-simple test-on-group-end-simple
    test-on-bad-count-simple test-on-bad-end-name-simple test-on-final-simple
    %run-assert %run-compare %run-approx %run-error %run-error-2 %run-group
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; bāqirum: "examiner, investigator" is the test-framework root;
    ;; pāqidum: "overseer, supervisor" for the test-runner object;
    ;; kiṣrum: "bundle" for a test-group; purussûm: "verdict" (reused
    ;; from the Catalan-number sense) for a test-result; kašādum:
    ;; "to reach/attain" for a name/pattern match; puzrum: "hidden
    ;; thing" marks the %-prefixed internal helpers.
    bāqir-kīnim 𒇽𒆜 bāqir-mitḫārim-eqv 𒅆𒆜𒈧 bāqir-mitḫārim-eq 𒋻𒊻 bāqir-
    mitḫārim-šalmim 𒈷𒆠 bāqir-qerbûm 𒀭𒆜𒇲 bāqir-ḫiṭītim 𒅆𒀀𒋻 bāqir-šemî-epšētim
    𒌋𒀸 šurrû-bāqirim 𒌋𒆜𒀀 gamār-bāqirim 𒂗𒉡𒇽 bāqir-kiṣrim 𒄀𒌑 bāqir-kiṣri-
    ullulim 𒅆𒆠𒇽 ēṭer-bāqirim 𒇽𒅆𒇽 qû-lemnêm 𒄀𒌑𒀭 kašād-šumim 𒁹𒆜𒆜 kašād-minîm
    𒌋𒃲𒀭 kašād-mimma 𒈷𒄀𒄀 kašād-gabbi 𒄿𒇽𒃲 pāqidum? 𒇽𒍪𒁀 pāqid-inanna 𒇲𒀸𒄿 maḫār-
    pāqidim 𒈠𒌑𒄿 epēš-pāqidim 𒄿𒆜 pāqid-lā-bašîm 𒌋𒌋𒈧 pāqid-ēdiš 𒇲𒄿𒀀 pāqid-
    bāniš 𒈧𒅆𒇽 nadān-bāqirim 𒉡𒄀 itti-pāqidim 𒁹𒄀𒈷 zikru-purussîm 𒌋𒄀 damqum-
    bāqirim? 𒈠𒄀 maḫār-purussîm 𒄿𒊻𒌝 šakān-purussîm 𒀭𒇲𒅆 nasāḫ-purussîm 𒇲𒇽𒈷
    ullul-purussîm 𒄿𒀀 purussû-ṭuppim 𒀸𒇲 mīnu-damqim 𒄀𒇽 mīnu-lemnim 𒅁𒈷𒃲 mīnu-
    damqi-lā-qaātim 𒃲𒂍 mīnu-lemni-lā-qaātim 𒊕𒌋 mīnu-eṭrim 𒄿𒁹 šum-bāqirim 𒃲𒇲
    urḫu-kiṣrim 𒆜𒋻 kiṣru-kīlum 𒈧𒁀 aḫu-qīštim 𒂗𒈧 šakān-aḫi-qīštim 𒆠𒊻𒊻 turru-
    pāqidim 𒌋𒌑 ina-šurrî-bāqirim 𒀸𒃲𒉡 šakān-ina-šurrî-bāqirim 𒂗𒀸𒉡 ina-gamār-
    bāqirim 𒂗𒅆𒅆 šakān-ina-gamār-bāqirim 𒂍𒋻 ina-šurrî-kiṣrim 𒂗𒂗 šakān-ina-
    šurrî-kiṣrim 𒄿𒀸𒂗 ina-gamār-kiṣrim 𒀸𒋻𒀭 šakān-ina-gamār-kiṣrim 𒄿𒌝𒋻 ina-
    lemni-mīnim 𒄀𒅁𒆠 šakān-ina-lemni-mīnim 𒆠𒀸𒈷 ina-lemni-šumim 𒇲𒃲𒂗 šakān-ina-
    lemni-šumim 𒈧𒌝𒉡 ina-gamrim 𒂍𒃲 šakān-ina-gamrim 𒇽𒈷𒁀 šurrû-bāqirim-ēdiš
    𒊻𒅆𒍪 gamār-bāqirim-ēdiš 𒀭𒅁 šurrû-kiṣrim-ēdiš 𒀀𒅆 gamār-kiṣrim-ēdiš 𒇽𒅁
    lemni-mīnim-ēdiš 𒅁𒌋𒄀 lemni-šumim-ēdiš 𒈷𒊕𒂍 gamrum-ēdiš 𒌝𒌑 bāqir-kīnim-
    puzrum 𒀀𒊕 kašād-puzrum 𒇽𒍪 qerbûm-puzrum 𒈠𒀸𒌑 ḫiṭītum-puzrum 𒊕𒂍 ḫiṭītum-
    puzrum-šanûm 𒇲𒇽 kiṣrum-puzrum 𒌋𒉡𒅁)
  (begin
    ;; test-assert/test-equal/.../test-group(-with-cleanup)/test-with-runner
    ;; are define-syntax in s64/testing.scm (they need to capture the
    ;; unevaluated source form), so their aliases must be macros too --
    ;; define-name-aliases's plain (define alias eng) would just bind
    ;; alias to a bogus non-procedure value, since eng isn't a value
    ;; reference at all when eng names a macro keyword.
    (define-syntax-aliases
      (test-assert              bāqir-kīnim                  𒇽𒆜)
      (test-eqv                 bāqir-mitḫārim-eqv           𒅆𒆜𒈧)
      (test-eq                  bāqir-mitḫārim-eq            𒋻𒊻)
      (test-equal               bāqir-mitḫārim-šalmim        𒈷𒆠)
      (test-approximate         bāqir-qerbûm                 𒀭𒆜𒇲)
      (test-error               bāqir-ḫiṭītim                𒅆𒀀𒋻)
      (test-group               bāqir-kiṣrim                 𒄀𒌑)
      (test-group-with-cleanup  bāqir-kiṣri-ullulim          𒅆𒆠𒇽)
      (test-with-runner         itti-pāqidim                 𒁹𒄀𒈷))
    (define-name-aliases
      (test-read-eval-string            bāqir-šemî-epšētim           𒌋𒀸)
      (test-begin                       šurrû-bāqirim                𒌋𒆜𒀀)
      (test-end                         gamār-bāqirim                𒂗𒉡𒇽)
      (test-skip                        ēṭer-bāqirim                 𒇽𒅆𒇽)
      (test-expect-fail                 qû-lemnêm                    𒄀𒌑𒀭)
      (test-match-name                  kašād-šumim                  𒁹𒆜𒆜)
      (test-match-nth                   kašād-minîm                  𒌋𒃲𒀭)
      (test-match-any                   kašād-mimma                  𒈷𒄀𒄀)
      (test-match-all                   kašād-gabbi                  𒄿𒇽𒃲)
      (test-runner?                     pāqidum?                     𒇽𒍪𒁀)
      (test-runner-current              pāqid-inanna                 𒇲𒀸𒄿)
      (test-runner-get                  maḫār-pāqidim                𒈠𒌑𒄿)
      (test-runner-create               epēš-pāqidim                 𒄿𒆜)
      (test-runner-null                 pāqid-lā-bašîm               𒌋𒌋𒈧)
      (test-runner-simple               pāqid-ēdiš                   𒇲𒄿𒀀)
      (test-runner-factory              pāqid-bāniš                  𒈧𒅆𒇽)
      (test-apply                       nadān-bāqirim                𒉡𒄀)
      (test-result-kind                 zikru-purussîm               𒌋𒄀)
      (test-passed?                     damqum-bāqirim?              𒈠𒄀)
      (test-result-ref                  maḫār-purussîm               𒄿𒊻𒌝)
      (test-result-set!                 šakān-purussîm               𒀭𒇲𒅆)
      (test-result-remove               nasāḫ-purussîm               𒇲𒇽𒈷)
      (test-result-clear                ullul-purussîm               𒄿𒀀)
      (test-result-alist                purussû-ṭuppim               𒀸𒇲)
      (test-runner-pass-count           mīnu-damqim                  𒄀𒇽)
      (test-runner-fail-count           mīnu-lemnim                  𒅁𒈷𒃲)
      (test-runner-xpass-count          mīnu-damqi-lā-qaātim         𒃲𒂍)
      (test-runner-xfail-count          mīnu-lemni-lā-qaātim         𒊕𒌋)
      (test-runner-skip-count           mīnu-eṭrim                   𒄿𒁹)
      (test-runner-test-name            šum-bāqirim                  𒃲𒇲)
      (test-runner-group-path           urḫu-kiṣrim                  𒆜𒋻)
      (test-runner-group-stack          kiṣru-kīlum                  𒈧𒁀)
      (test-runner-aux-value            aḫu-qīštim                   𒂗𒈧)
      (test-runner-aux-value!           šakān-aḫi-qīštim             𒆠𒊻𒊻)
      (test-runner-reset                turru-pāqidim                𒌋𒌑)
      (test-runner-on-test-begin        ina-šurrî-bāqirim            𒀸𒃲𒉡)
      (test-runner-on-test-begin!       šakān-ina-šurrî-bāqirim      𒂗𒀸𒉡)
      (test-runner-on-test-end          ina-gamār-bāqirim            𒂗𒅆𒅆)
      (test-runner-on-test-end!         šakān-ina-gamār-bāqirim      𒂍𒋻)
      (test-runner-on-group-begin       ina-šurrî-kiṣrim             𒂗𒂗)
      (test-runner-on-group-begin!      šakān-ina-šurrî-kiṣrim       𒄿𒀸𒂗)
      (test-runner-on-group-end         ina-gamār-kiṣrim             𒀸𒋻𒀭)
      (test-runner-on-group-end!        šakān-ina-gamār-kiṣrim       𒄿𒌝𒋻)
      (test-runner-on-bad-count         ina-lemni-mīnim              𒄀𒅁𒆠)
      (test-runner-on-bad-count!        šakān-ina-lemni-mīnim        𒆠𒀸𒈷)
      (test-runner-on-bad-end-name      ina-lemni-šumim              𒇲𒃲𒂗)
      (test-runner-on-bad-end-name!     šakān-ina-lemni-šumim        𒈧𒌝𒉡)
      (test-runner-on-final             ina-gamrim                   𒂍𒃲)
      (test-runner-on-final!            šakān-ina-gamrim             𒇽𒈷𒁀)
      (test-on-test-begin-simple        šurrû-bāqirim-ēdiš           𒊻𒅆𒍪)
      (test-on-test-end-simple          gamār-bāqirim-ēdiš           𒀭𒅁)
      (test-on-group-begin-simple       šurrû-kiṣrim-ēdiš            𒀀𒅆)
      (test-on-group-end-simple         gamār-kiṣrim-ēdiš            𒇽𒅁)
      (test-on-bad-count-simple         lemni-mīnim-ēdiš             𒅁𒌋𒄀)
      (test-on-bad-end-name-simple      lemni-šumim-ēdiš             𒈷𒊕𒂍)
      (test-on-final-simple             gamrum-ēdiš                  𒌝𒌑)
      (%run-assert                      bāqir-kīnim-puzrum           𒀀𒊕)
      (%run-compare                     kašād-puzrum                 𒇽𒍪)
      (%run-approx                      qerbûm-puzrum                𒈠𒀸𒌑)
      (%run-error                       ḫiṭītum-puzrum               𒊕𒂍)
      (%run-error-2                     ḫiṭītum-puzrum-šanûm         𒇲𒇽)
      (%run-group                       kiṣrum-puzrum                𒌋𒉡𒅁))))
