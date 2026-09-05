#!/usr/bin/env curry
;;; basic_interpreter.scm -- a small interpreter for a Dartmouth-style BASIC
;;;
;;; This file is meant to be *read*, not just run: it walks through the four
;;; stages every line-oriented interpreter goes through, in the order a
;;; source line actually flows through them:
;;;
;;;   1. TOKENIZE   raw text        -> a flat list of tokens
;;;   2. PARSE      tokens          -> an AST (nested tagged lists)
;;;   3. LOAD       many lines      -> one program: a vector of statements
;;;                                    plus a line-number -> index map
;;;   4. EXECUTE    program + env   -> side effects (PRINT, variables, etc.)
;;;
;;; Stages 1-3 each run exactly once per line, at load time. Stage 4 is the
;;; only thing that runs repeatedly, so it's the only stage where constant
;;; factors matter -- see the "Why a vector, not a list" and "Why hash
;;; tables" notes below for the two efficiency decisions that follow from
;;; that observation.
;;;
;;; Supported dialect (deliberately small, but not a toy):
;;;   LET (optional keyword), PRINT, INPUT, IF/THEN, GOTO, GOSUB/RETURN,
;;;   FOR/TO/STEP/NEXT, DIM (1-D arrays), END/STOP, REM.
;;;   Numeric and string variables (trailing $ marks a string variable/array).
;;;   Expressions: + - * / ^, unary -, = <> < > <= >=, AND OR NOT,
;;;   parentheses, and functions ABS INT SQR RND LEN VAL STR$ CHR$ ASC
;;;   LEFT$ RIGHT$ MID$ TAB.
;;;   Multiple statements per line separated by ':'.
;;;
;;; Run it with no arguments to execute the built-in demo program, or
;;; `curry basic_interpreter.scm yourprogram.bas` to run a file.

;;; ------------------------------------------------------------------
;;; 1. TOKENIZER
;;;
;;; A token is a pair (TAG . VALUE). Turning text into tokens up front
;;; means the parser below never has to think about whitespace, case,
;;; or where one lexeme ends and the next begins -- it only ever looks
;;; at the next pair in the list.
;;; ------------------------------------------------------------------

(define (ident-start? c) (char-alphabetic? c))
(define (ident-cont? c) (or (char-alphabetic? c) (char-numeric? c)))

;; Scan one BASIC source line into a list of tokens. Identifiers are
;; upper-cased as they're read so keyword matching later (PRINT vs print)
;; and variable lookup are both simple string equality -- BASIC variable
;; names are traditionally case-insensitive.
(define (tokenize line)
  (let ((len (string-length line)))
    (let loop ((i 0) (acc '()))
      (if (>= i len)
          (reverse acc)
          (let ((c (string-ref line i)))
            (cond
              ;; whitespace: skip
              ((char-whitespace? c) (loop (+ i 1) acc))

              ;; comment to end of line: REM is handled as a keyword
              ;; instead (it still needs to consume the rest of the
              ;; line, but that's a parser concern, not a lexer one)

              ;; number literal: digits with an optional single '.'
              ((char-numeric? c)
               (let scan ((j i) (seen-dot #f))
                 (cond
                   ((and (< j len) (char-numeric? (string-ref line j)))
                    (scan (+ j 1) seen-dot))
                   ((and (< j len) (char=? (string-ref line j) #\.) (not seen-dot))
                    (scan (+ j 1) #t))
                   (else
                    (loop j (cons (cons 'num (string->number (substring line i j))) acc))))))

              ;; string literal: "...", no escape sequences (matches
              ;; classic BASIC, which had none either)
              ((char=? c #\")
               (let scan ((j (+ i 1)))
                 (cond
                   ((>= j len) (error "basic: unterminated string literal" line))
                   ((char=? (string-ref line j) #\")
                    (loop (+ j 1) (cons (cons 'str (substring line (+ i 1) j)) acc)))
                   (else (scan (+ j 1))))))

              ;; identifier / keyword, optionally trailing '$' for a
              ;; string-typed variable or function name (LEN$ etc.)
              ((ident-start? c)
               (let scan ((j (+ i 1)))
                 (if (and (< j len) (ident-cont? (string-ref line j)))
                     (scan (+ j 1))
                     (let ((j2 (if (and (< j len) (char=? (string-ref line j) #\$)) (+ j 1) j)))
                       (loop j2 (cons (cons 'ident (string-upcase (substring line i j2))) acc))))))

              ;; two-character operators must be tried before their
              ;; one-character prefixes (<= before <, <> before <)
              ((and (char=? c #\<) (< (+ i 1) len) (char=? (string-ref line (+ i 1)) #\=))
               (loop (+ i 2) (cons (cons 'op "<=") acc)))
              ((and (char=? c #\<) (< (+ i 1) len) (char=? (string-ref line (+ i 1)) #\>))
               (loop (+ i 2) (cons (cons 'op "<>") acc)))
              ((and (char=? c #\>) (< (+ i 1) len) (char=? (string-ref line (+ i 1)) #\=))
               (loop (+ i 2) (cons (cons 'op ">=") acc)))

              ((char=? c #\() (loop (+ i 1) (cons (cons 'lparen #f) acc)))
              ((char=? c #\)) (loop (+ i 1) (cons (cons 'rparen #f) acc)))
              ((char=? c #\,) (loop (+ i 1) (cons (cons 'comma #f) acc)))
              ((char=? c #\;) (loop (+ i 1) (cons (cons 'semi #f) acc)))
              ((char=? c #\:) (loop (+ i 1) (cons (cons 'colon #f) acc)))
              ((memv c '(#\+ #\- #\* #\/ #\^ #\= #\< #\>))
               (loop (+ i 1) (cons (cons 'op (string c)) acc)))

              (else (error "basic: unexpected character" c line))))))))

;;; ------------------------------------------------------------------
;;; 2. EXPRESSION PARSER (precedence climbing)
;;;
;;; Each level below handles exactly one precedence tier and calls the
;;; next-tighter level for its operands. This is the standard way to
;;; turn a grammar's precedence table directly into code: the call
;;; chain top-to-bottom mirrors loosest-to-tightest binding, and adding
;;; a new operator at some precedence means editing exactly one level.
;;;
;;; Precedence, loosest to tightest:
;;;   OR
;;;   AND
;;;   NOT (unary)
;;;   =  <>  <  >  <=  >=
;;;   +  -
;;;   *  /
;;;   ^                (right-associative)
;;;   unary -
;;;   primary: literals, variables, array refs, function calls, ( expr )
;;;
;;; Every parse-* function takes a token list and returns (values ast
;;; rest-of-tokens) via a cons: (cons ast rest). Curry's reader doesn't
;;; need multiple-value returns here, so a cons pair is simpler than
;;; call-with-values plumbing throughout.
;;; ------------------------------------------------------------------

(define (tok-tag tok) (car tok))
(define (tok-val tok) (cdr tok))

(define (peek-tag toks) (if (null? toks) 'eof (tok-tag (car toks))))
(define (peek-ident toks) (and (pair? toks) (eq? (tok-tag (car toks)) 'ident) (tok-val (car toks))))
(define (peek-op toks) (and (pair? toks) (eq? (tok-tag (car toks)) 'op) (tok-val (car toks))))

(define (expect-tag toks tag what)
  (if (and (pair? toks) (eq? (tok-tag (car toks)) tag))
      (cdr toks)
      (error (string-append "basic: expected " what) toks)))

(define (parse-expr toks) (parse-or toks))

(define (parse-or toks)
  (let loop ((r (parse-and toks)))
    (let ((ast (car r)) (rest (cdr r)))
      (if (equal? (peek-ident rest) "OR")
          (let ((r2 (parse-and (cdr rest))))
            (loop (cons (list 'or ast (car r2)) (cdr r2))))
          r))))

(define (parse-and toks)
  (let loop ((r (parse-not toks)))
    (let ((ast (car r)) (rest (cdr r)))
      (if (equal? (peek-ident rest) "AND")
          (let ((r2 (parse-not (cdr rest))))
            (loop (cons (list 'and ast (car r2)) (cdr r2))))
          r))))

(define (parse-not toks)
  (if (equal? (peek-ident toks) "NOT")
      (let ((r (parse-not (cdr toks))))
        (cons (list 'not (car r)) (cdr r)))
      (parse-cmp toks)))

(define (parse-cmp toks)
  (let* ((r (parse-add toks)) (ast (car r)) (rest (cdr r)) (op (peek-op rest)))
    (if (member op '("=" "<>" "<" ">" "<=" ">="))
        (let ((r2 (parse-add (cdr rest))))
          (cons (list 'cmp op ast (car r2)) (cdr r2)))
        r)))

(define (parse-add toks)
  (let loop ((r (parse-mul toks)))
    (let* ((ast (car r)) (rest (cdr r)) (op (peek-op rest)))
      (if (member op '("+" "-"))
          (let ((r2 (parse-mul (cdr rest))))
            (loop (cons (list 'binop op ast (car r2)) (cdr r2))))
          r))))

(define (parse-mul toks)
  (let loop ((r (parse-pow toks)))
    (let* ((ast (car r)) (rest (cdr r)) (op (peek-op rest)))
      (if (member op '("*" "/"))
          (let ((r2 (parse-pow (cdr rest))))
            (loop (cons (list 'binop op ast (car r2)) (cdr r2))))
          r))))

;; ^ is right-associative: 2^3^2 = 2^(3^2), so the recursive call on the
;; right-hand side is to parse-pow itself, not to a tighter level.
(define (parse-pow toks)
  (let* ((r (parse-unary toks)) (ast (car r)) (rest (cdr r)))
    (if (equal? (peek-op rest) "^")
        (let ((r2 (parse-pow (cdr rest))))
          (cons (list 'binop "^" ast (car r2)) (cdr r2)))
        r)))

(define (parse-unary toks)
  (if (equal? (peek-op toks) "-")
      (let ((r (parse-unary (cdr toks))))
        (cons (list 'neg (car r)) (cdr r)))
      (parse-primary toks)))

(define (parse-primary toks)
  (if (null? toks) (error "basic: unexpected end of expression"))
  (let ((tag (tok-tag (car toks))) (val (tok-val (car toks))))
    (cond
      ((eq? tag 'num) (cons (list 'lit val) (cdr toks)))
      ((eq? tag 'str) (cons (list 'lit val) (cdr toks)))
      ((eq? tag 'lparen)
       (let* ((r (parse-expr (cdr toks))))
         (cons (car r) (expect-tag (cdr r) 'rparen "')'"))))
      ((eq? tag 'ident)
       (cond
         ;; function call or array reference: NAME ( args... )
         ((and (pair? (cdr toks)) (eq? (tok-tag (cadr toks)) 'lparen))
          (let loop ((r (parse-expr (cddr toks))) (args '()))
            (let ((args2 (cons (car r) args)))
              (cond
                ((equal? (peek-tag (cdr r)) 'comma) (loop (parse-expr (cdr (cdr r))) args2))
                (else (cons (list 'call val (reverse args2)) (expect-tag (cdr r) 'rparen "')'")))))))
         (else (cons (list 'var val) (cdr toks)))))
      (else (error "basic: unexpected token in expression" (car toks))))))

;;; ------------------------------------------------------------------
;;; 3. STATEMENT PARSER
;;;
;;; A statement AST is a tagged list whose head names the statement and
;;; whose tail holds its already-parsed pieces (sub-expressions, other
;;; statements, ...). Parsing each statement fully at load time -- down
;;; to its expression ASTs -- means the execute loop in section 5 never
;;; re-tokenizes or re-parses anything, no matter how many times a GOTO
;;; sends control back through the same line.
;;; ------------------------------------------------------------------

(define (parse-print-list toks)
  ;; Comma inserts a tab, semicolon inserts nothing, and the item that
  ;; ends the list (because nothing follows it, or a ':' does) instead
  ;; gets a trailing newline -- each item's own separator, carried in
  ;; its cdr, is what exec-print later displays after it, so there is
  ;; no separate "suppress the newline" flag to track: the newline
  ;; either belongs to the last item or it doesn't, and that's decided
  ;; right here, once. The end-of-list check has to run on *every*
  ;; iteration, not just before the first: "PRINT X;" with nothing
  ;; after the trailing ';' must stop the loop instead of trying to
  ;; parse another expression out of an empty token list. Returns
  ;; (list items leftover-tokens) -- the second element matters
  ;; whenever a PRINT is followed by a ':'-separated statement.
  (let loop ((toks toks) (items '()))
    (if (or (null? toks) (eq? (peek-tag toks) 'colon))
        (list (reverse items) toks)
        (let* ((r (parse-expr toks)) (rest (cdr r)))
          (cond
            ((eq? (peek-tag rest) 'comma) (loop (cdr rest) (cons (cons (car r) "\t") items)))
            ((eq? (peek-tag rest) 'semi) (loop (cdr rest) (cons (cons (car r) "") items)))
            (else (list (reverse (cons (cons (car r) "\n") items)) rest)))))))

;; Parses one statement, returning (cons ast rest-of-tokens). IF is the
;; one statement that needs to recurse into parse-statement-list (for
;; its THEN branch) before parse-statement-list itself is defined below
;; -- fine in Scheme, since nothing calls parse-statement until after
;; the whole file has loaded and every forward reference has resolved.
(define (parse-statement toks)
  (let ((kw (peek-ident toks)))
    (cond
      ((not kw) (error "basic: statement expected" toks))
      ((equal? kw "REM") (cons (list 'rem) '()))
      ((equal? kw "END") (cons (list 'end) (cdr toks)))
      ((equal? kw "STOP") (cons (list 'end) (cdr toks)))
      ((equal? kw "RETURN") (cons (list 'return) (cdr toks)))
      ((equal? kw "GOTO")
       (let ((r (parse-expr (cdr toks)))) (cons (list 'goto (car r)) (cdr r))))
      ((equal? kw "GOSUB")
       (let ((r (parse-expr (cdr toks)))) (cons (list 'gosub (car r)) (cdr r))))
      ((equal? kw "LET") (parse-assignment (cdr toks)))
      ((equal? kw "PRINT")
       (let ((pr (parse-print-list (cdr toks))))
         (cons (list 'print (car pr)) (cadr pr))))
      ((equal? kw "INPUT")
       (let loop ((toks (cdr toks)) (names '()))
         (let ((name (peek-ident toks)))
           (if (not name) (error "basic: variable name expected after INPUT"))
           (if (equal? (peek-tag (cdr toks)) 'comma)
               (loop (cddr toks) (cons name names))
               (cons (list 'input (reverse (cons name names))) (cdr toks))))))
      ((equal? kw "DIM")
       (let loop ((toks (cdr toks)) (specs '()))
         (let ((name (peek-ident toks)))
           (if (not name) (error "basic: array name expected after DIM" toks))
           (let* ((rest0 (expect-tag (cdr toks) 'lparen "'(' after array name"))
                  (r (parse-expr rest0))
                  (rest (expect-tag (cdr r) 'rparen "')'"))
                  (specs2 (cons (cons name (car r)) specs)))
             (if (equal? (peek-tag rest) 'comma)
                 (loop (cdr rest) specs2)
                 (cons (list 'dim (reverse specs2)) rest))))))
      ((equal? kw "FOR")
       (let ((name (peek-ident (cdr toks))))
         (if (not name) (error "basic: variable name expected after FOR" toks))
         (let* ((rest0 (expect-equals (cddr toks)))    ; past FOR NAME '='
                (r1 (parse-expr rest0))
                (rest1 (cdr r1)))
           (if (not (equal? (peek-ident rest1) "TO"))
               (error "basic: TO expected in FOR" rest1))
           (let* ((r2 (parse-expr (cdr rest1)))         ; past 'TO'
                  (rest2 (cdr r2)))
             (if (equal? (peek-ident rest2) "STEP")
                 (let ((r3 (parse-expr (cdr rest2))))
                   (cons (list 'for name (car r1) (car r2) (car r3)) (cdr r3)))
                 (cons (list 'for name (car r1) (car r2) (list 'lit 1)) rest2))))))
      ((equal? kw "NEXT")
       ;; A bare "NEXT" (closing the innermost loop without naming it) is
       ;; valid -- run-program's 'next case falls back to the innermost
       ;; for-stack frame when no name is given -- so this must not
       ;; assume a variable-name token follows; only consume one if
       ;; there actually is one.
       (let ((rest (cdr toks)))
         (if (and (pair? rest) (eq? (tok-tag (car rest)) 'ident))
             (cons (list 'next (tok-val (car rest))) (cdr rest))
             (cons (list 'next #f) rest))))
      ((equal? kw "IF")
       (let* ((r (parse-expr (cdr toks)))
              (cond-ast (car r))
              (rest (cdr r)))
         (if (not (equal? (peek-ident rest) "THEN"))
             (error "basic: THEN expected in IF" rest))
         (let ((after-then (cdr rest)))
           ;; "IF x THEN 100" is shorthand for "IF x THEN GOTO 100". Like
           ;; the general THEN clause, everything after it on the line
           ;; still belongs to the conditional -- it must not leak back
           ;; out as unconditional top-level statements just because
           ;; this branch is a shorthand. (It's true that a GOTO always
           ;; jumps away immediately, so any further ':'-separated
           ;; statement in this same then-clause is unreachable when
           ;; the condition is true -- but that's exactly the point:
           ;; when the condition is *false*, none of it should run
           ;; either, which only holds if it's inside the then-clause.)
           (if (eq? (peek-tag after-then) 'num)
               (let* ((goto-stmt (list 'goto (list 'lit (tok-val (car after-then)))))
                      (remaining (cdr after-then))
                      (more-stmts (if (and (pair? remaining) (eq? (tok-tag (car remaining)) 'colon))
                                      (parse-statement-list (cdr remaining))
                                      '())))
                 (cons (list 'if cond-ast (cons goto-stmt more-stmts)) '()))
               (let ((then-stmts (parse-statement-list after-then)))
                 (cons (list 'if cond-ast then-stmts) '()))))))
      (else (parse-assignment toks)))))

;; ASSIGN, either "LET" was already consumed or BASIC's classic
;; LET-is-optional rule applies: a bare "A = 1" or "A(I) = 1" is still
;; an assignment. Distinguishing "A(1) = ..." (array store) from
;; "A(1)" appearing mid-expression is why array assignment is handled
;; here rather than falling out of parse-primary.
(define (expect-equals toks)
  (if (equal? (peek-op toks) "=") (cdr toks) (error "basic: expected '='" toks)))

(define (parse-assignment toks)
  (let ((name (peek-ident toks)))
    (if (not name) (error "basic: assignment expected" toks))
    (if (and (pair? (cdr toks)) (eq? (tok-tag (cadr toks)) 'lparen))
        (let* ((r (parse-expr (cddr toks)))
               (rest (expect-tag (cdr r) 'rparen "')'"))
               (rest2 (expect-equals rest))
               (rhs (parse-expr rest2)))
          ;; The tail of `rhs` (not '()) is what's left of the line --
          ;; e.g. a colon-separated statement following this one on
          ;; the same source line -- and must be passed back so
          ;; parse-statement-list can keep parsing it instead of
          ;; silently dropping the rest of the line.
          (cons (list 'let-arr name (car r) (car rhs)) (cdr rhs)))
        (let* ((rest (expect-equals (cdr toks)))
               (rhs (parse-expr rest)))
          (cons (list 'let name (car rhs)) (cdr rhs))))))

;; Splits one line's tokens on top-level ':' into a list of statement
;; ASTs. IF/THEN is special: everything after THEN belongs to the
;; consequent, so parse-statement already consumed the rest of the
;; token list for an IF and returns '() -- the loop below stops
;; naturally once tokens run out.
(define (parse-statement-list toks)
  (if (null? toks)
      '()
      (let ((r (parse-statement toks)))
        (cons (car r)
              (if (and (pair? (cdr r)) (eq? (peek-tag (cdr r)) 'colon))
                  (parse-statement-list (cdr (cdr r)))
                  (parse-statement-list (cdr r)))))))

;;; ------------------------------------------------------------------
;;; 4. PROGRAM LOADER
;;;
;;; Why a vector, not a list: GOTO/GOSUB/NEXT all jump to an arbitrary
;;; statement by index, potentially millions of times in a loop. A list
;;; makes that an O(n) walk from the head every time; a vector makes it
;;; O(1) sequential-execute-then-index. So the loader's job is to turn
;;; "many lines of text" into one flat vector of (line-number . stmt)
;;; pairs, plus a hash table mapping each line number to the vector
;;; index of its *first* statement (a line can hold several,
;;; colon-separated) -- that hash table is what makes GOTO O(1) instead
;;; of a linear scan for the target line number.
;;; ------------------------------------------------------------------

;; IF/THEN's consequent is parsed as a *list* of statements (see parse-
;; statement's 'if case), but the flat vector below is what makes
;; GOTO/GOSUB/NEXT O(1), and a GOSUB inside a THEN clause still needs
;; its RETURN to come back to "the statement right after the GOSUB" --
;; which might be another statement in that same THEN clause, not the
;; next program line. Both requirements are satisfied the way a real
;; compiler handles this: splice the THEN clause's statements directly
;; into the flat vector as ordinary, individually-addressable slots,
;; replacing 'if with a conditional skip -- 'if-skip cond n -- that
;; falls through into the (now-adjacent) then-statements when cond is
;; true, or jumps past all n of them when it's false. An 'if nested
;; inside a THEN clause is flattened the same way, recursively, so
;; nesting depth doesn't matter.
(define (flatten-stmts stmt-list)
  (apply append
         (map (lambda (s)
                (if (eq? (car s) 'if)
                    (let ((then-flat (flatten-stmts (caddr s))))
                      (cons (list 'if-skip (cadr s) (length then-flat)) then-flat))
                    (list s)))
              stmt-list)))

(define (load-program source-lines)
  ;; `count` tracks how many statements have been appended so far, so
  ;; each line's starting index is O(1) to record. Using `(length
  ;; stmts)` instead (which was tried first) looks equivalent but
  ;; isn't: `stmts` is the accumulator for the *entire* program, so
  ;; walking it to compute one line's offset is O(so-far-total), and
  ;; doing that once per line makes the whole load pass O(N^2) in the
  ;; total statement count instead of the O(N) the vector+hash-table
  ;; design (see the "Why a vector" note above) is supposed to buy.
  (let ((stmts '()) (count 0) (line-index (make-hash-table)))
    (for-each
     (lambda (raw)
       (let* ((trimmed (string-trim raw)))
         (when (> (string-length trimmed) 0)
           (let* ((sp (or (string-contains trimmed " ") (string-length trimmed)))
                  (lineno (string->number (substring trimmed 0 sp)))
                  (rest (if (< sp (string-length trimmed)) (substring trimmed (+ sp 1) (string-length trimmed)) "")))
             (if (not lineno) (error "basic: line missing a line number" raw))
             (when (hash-table-exists? line-index lineno)
               (error "basic: duplicate line number" lineno))
             (hash-table-set! line-index lineno count)
             (for-each (lambda (s)
                         (set! stmts (cons (cons lineno s) stmts))
                         (set! count (+ count 1)))
                       (flatten-stmts (parse-statement-list (tokenize rest))))))))
     source-lines)
    (list (list->vector (reverse stmts)) line-index)))

(define (string-trim s)
  (let* ((len (string-length s))
         (start (let loop ((i 0)) (if (and (< i len) (char-whitespace? (string-ref s i))) (loop (+ i 1)) i)))
         (end (let loop ((i len)) (if (and (> i start) (char-whitespace? (string-ref s (- i 1)))) (loop (- i 1)) i))))
    (substring s start end)))

;;; ------------------------------------------------------------------
;;; 5. RUNTIME: environment, expression evaluation, statement execution
;;;
;;; Why hash tables: BASIC variables are created on first assignment,
;;; not declared, and any of A-Z (times however many digits/letters you
;;; allow, plus a $ suffix) may appear -- an unbounded, sparse name
;;; space. A hash table gives O(1) expected lookup/store without
;;; needing to know the variable set in advance, which is exactly the
;;; SRFI-69-style table the language.md guide documents.
;;; ------------------------------------------------------------------

(define (make-env) (make-hash-table))                     ; name -> scalar value
(define (make-arrays) (make-hash-table))                  ; name -> vector

;; BASIC's traditional boolean encoding: -1 is true, 0 is false, and
;; any nonzero number is truthy when *read* as a condition (so
;; "IF X THEN ..." works after "LET X = -1").
(define (truthy? v) (and (number? v) (not (= v 0))))
(define (bool->basic v) (if v -1 0))

(define (basic-error msg . irritants)
  (apply error (string-append "basic runtime error: " msg) irritants))

(define (env-get env name)
  (hash-table-ref env name (if (string-suffix-$? name) "" 0)))

(define (string-suffix-$? name)
  (and (> (string-length name) 0) (char=? (string-ref name (- (string-length name) 1)) #\$)))

;; Evaluates one expression AST against the current variable/array state.
(define (eval-expr ast env arrays)
  (case (car ast)
    ((lit) (cadr ast))
    ((var) (env-get env (cadr ast)))
    ((neg) (- (eval-expr (cadr ast) env arrays)))
    ((not) (bool->basic (not (truthy? (eval-expr (cadr ast) env arrays)))))
    ((and) (bool->basic (and (truthy? (eval-expr (cadr ast) env arrays))
                              (truthy? (eval-expr (caddr ast) env arrays)))))
    ((or) (bool->basic (or (truthy? (eval-expr (cadr ast) env arrays))
                            (truthy? (eval-expr (caddr ast) env arrays)))))
    ((cmp) (eval-cmp (cadr ast) (eval-expr (caddr ast) env arrays) (eval-expr (car (cdddr ast)) env arrays)))
    ((binop) (eval-binop (cadr ast) (eval-expr (caddr ast) env arrays) (eval-expr (car (cdddr ast)) env arrays)))
    ((call) (eval-call-or-index (cadr ast) (map (lambda (a) (eval-expr a env arrays)) (caddr ast)) env arrays))
    (else (basic-error "malformed expression" ast))))

;; Ordering (< > <= >=) has to dispatch on operand type: BASIC lets you
;; alphabetize with string variables ("IF A$ < B$ THEN ...") just as
;; often as it compares numbers, but curry's own </>/<=/>=  are numeric
;; -only and raise on a string argument, so a string comparison needs
;; string<?/string>? instead. = and <> stay generic either way, since
;; equal? already handles both.
(define (eval-cmp op a b)
  (bool->basic
   (cond
     ((equal? op "=")  (equal? a b))
     ((equal? op "<>") (not (equal? a b)))
     ((and (string? a) (string? b))
      (cond
        ((equal? op "<")  (string<? a b))
        ((equal? op ">")  (string>? a b))
        ((equal? op "<=") (string<=? a b))
        ((equal? op ">=") (string>=? a b))
        (else (basic-error "unknown comparison operator" op))))
     ((equal? op "<")  (< a b))
     ((equal? op ">")  (> a b))
     ((equal? op "<=") (<= a b))
     ((equal? op ">=") (>= a b))
     (else (basic-error "unknown comparison operator" op)))))

(define (eval-binop op a b)
  (cond
    ;; "+" doubles as numeric add and string concatenation, matching
    ;; classic BASIC (which had no separate concatenation operator).
    ((equal? op "+") (if (and (string? a) (string? b)) (string-append a b) (+ a b)))
    ((equal? op "-") (- a b))
    ((equal? op "*") (* a b))
    ((equal? op "/") (/ a b))
    ((equal? op "^") (expt a b))
    (else (basic-error "unknown operator" op))))

;; Converts a BASIC numeric value to an exact integer suitable for use
;; as an array index, string length, or character count. `inexact->exact`
;; alone is *not* enough: it only changes representation, not value, so
;; `(inexact->exact 2.5)` is `5/2`, not `2` or `3` -- and since curry's
;; `/` on two exact integers yields an exact rational rather than a
;; flonum (`(/ 5 2)` => `5/2`, already "exact"), this bites even
;; without floating point: `A(I/2)` needs truncation just as much as
;; `A(I/2.0)` does. Truncating (not rounding) matches classic BASIC's
;; INT-like coercion for these contexts.
(define (->int x) (inexact->exact (floor x)))

;; A bare NAME(args) is ambiguous at parse time between "array element"
;; and "function call" -- both look identical in the grammar. It's
;; resolved here, at evaluation time, by name: a handful of reserved
;; words are functions, and DIM'd names in `arrays` are indexing; a
;; NAME(...) that is neither is an error (undeclared array).
(define (eval-call-or-index name args env arrays)
  (cond
    ;; A single hash-table-ref, not `hash-table-exists?` followed by a
    ;; separate `hash-table-ref` -- arrays never legitimately store #f
    ;; (DIM always initializes elements to "" or 0), so #f unambiguously
    ;; means "not an array," and this is on the hot path (every array
    ;; read inside a FOR/NEXT loop), where a second hash probe per
    ;; access is wasted work.
    ((hash-table-ref arrays name #f) => (lambda (vec) (vector-ref vec (->int (car args)))))
    ((equal? name "ABS")   (abs (car args)))
    ((equal? name "INT")   (->int (car args)))
    ((equal? name "SQR")
     (if (< (car args) 0)
         (basic-error "SQR of a negative number" (car args))
         (sqrt (car args))))
    ((equal? name "RND")   (random-real))
    ((equal? name "LEN")   (string-length (car args)))
    ((equal? name "VAL")   (or (string->number (car args)) 0))
    ((equal? name "STR$")  (number->string (car args)))
    ((equal? name "CHR$")  (string (integer->char (->int (car args)))))
    ((equal? name "ASC")   (char->integer (string-ref (car args) 0)))
    ((equal? name "TAB")   (make-string (max 0 (->int (car args))) #\space))
    ((equal? name "LEFT$") (substring (car args) 0 (min (string-length (car args)) (->int (cadr args)))))
    ((equal? name "RIGHT$")
     (let* ((s (car args)) (n (->int (cadr args))) (len (string-length s)))
       (substring s (max 0 (- len n)) len)))
    ((equal? name "MID$")
     (let* ((s (car args)) (start (- (->int (cadr args)) 1))
            (n (if (pair? (cddr args)) (->int (caddr args)) (- (string-length s) start))))
       (substring s start (min (string-length s) (+ start n)))))
    (else (basic-error "undefined array or function" name))))

;;; ------------------------------------------------------------------
;;; STATEMENT EXECUTION
;;;
;;; `run-program` is the interpreter's heart. It is one self-tail-call
;;; per statement executed: `(run pc gosub-stack for-stack)` always
;;; ends by calling itself in tail position (see every branch below),
;;; so Curry's TCO (`goto tail` in the VM, per the eval() docs) turns
;;; the whole execution into a flat loop -- a program with a million
;;; iterations of a FOR/NEXT loop, or a GOTO that jumps backward
;;; forever, does not grow the C call stack at all. Each statement's
;;; own handling below stays equally non-recursive: it never calls
;;; back into `run`, it just picks the pc (and gosub-/for-stack) to
;;; tail-call `run` with next -- including IF, whose THEN clause was
;;; already spliced into this same vector at load time (see
;;; flatten-stmts), so it needs no execution path of its own.
;;; ------------------------------------------------------------------

(define (find-line line-index n)
  (or (hash-table-ref line-index n #f)
      (basic-error "no such line number" n)))

;; Each item already carries its own trailing separator ("\t", "", or
;; "\n") in its cdr, decided once by parse-print-list, so there is
;; nothing left for exec-print to decide -- except that a completely
;; bare "PRINT" (no items at all, so no item is left to carry a "\n")
;; must still emit the blank line classic BASIC uses it for.
(define (exec-print items env arrays)
  (if (null? items)
      (newline)
      (for-each (lambda (item)
                  (let ((v (eval-expr (car item) env arrays)))
                    (display (if (string? v) v (number->string v))))
                  (display (cdr item)))
                items)))

(define (exec-input names env)
  (for-each
   (lambda (name)
     (display "? ") (flush-output-port (current-output-port))
     (let ((line (read-line)))
       (if (eof-object? line)
           (basic-error "unexpected end of input in INPUT")
           (hash-table-set! env name
                             (if (string-suffix-$? name) line (or (string->number line) 0))))))
   names))

;; eval-call-or-index resolves a bare NAME(args) by checking `arrays`
;; before it checks the builtin-function names, so DIM'ing an array
;; under a builtin's name (DIM RND(5)) would silently shadow that
;; builtin for the rest of the program instead of erroring -- a
;; startling, hard-to-debug way to lose e.g. random numbers. Rejecting
;; it here, at the one place arrays get created, is cheaper than
;; teaching every call site to break the tie.
(define builtin-function-names
  '("ABS" "INT" "SQR" "RND" "LEN" "VAL" "STR$" "CHR$" "ASC" "TAB" "LEFT$" "RIGHT$" "MID$"))

(define (exec-dim specs env arrays)
  (for-each (lambda (spec)
              (let* ((name (car spec)) (size (->int (eval-expr (cdr spec) env arrays))))
                (when (member name builtin-function-names)
                  (basic-error "cannot DIM an array with the same name as a builtin function" name))
                (hash-table-set! arrays name (make-vector (+ size 1) (if (string-suffix-$? name) "" 0)))))
            specs))

;;; The main loop. pc is a vector index into `stmts`; gosub-stack is a
;;; list of return pcs; for-stack is a list of (name limit step body-pc)
;;; frames, most recent first, so NEXT without a matching FOR name
;;; simply searches outward -- exactly like a dynamic-scope lookup.
(define (run-program stmts line-index env arrays)
  (let ((n (vector-length stmts)))
    (let run ((pc 0) (gosub-stack '()) (for-stack '()))
      (if (>= pc n)
          'done                                            ; fell off the end of the program
          (let* ((entry (vector-ref stmts pc))
                 (stmt (cdr entry)))
            (case (car stmt)

              ((rem) (run (+ pc 1) gosub-stack for-stack))

              ((end) 'done)

              ((let)
               (hash-table-set! env (cadr stmt) (eval-expr (caddr stmt) env arrays))
               (run (+ pc 1) gosub-stack for-stack))

              ((let-arr)
               (let* ((name (cadr stmt))
                      (idx (->int (eval-expr (caddr stmt) env arrays)))
                      (val (eval-expr (car (cdddr stmt)) env arrays))
                      (vec (or (hash-table-ref arrays name #f) (basic-error "array not DIMensioned" name))))
                 (vector-set! vec idx val))
               (run (+ pc 1) gosub-stack for-stack))

              ((print)
               (exec-print (cadr stmt) env arrays)
               (run (+ pc 1) gosub-stack for-stack))

              ((input)
               (exec-input (cadr stmt) env)
               (run (+ pc 1) gosub-stack for-stack))

              ((dim)
               (exec-dim (cadr stmt) env arrays)
               (run (+ pc 1) gosub-stack for-stack))

              ((goto)
               (run (find-line line-index (->int (eval-expr (cadr stmt) env arrays))) gosub-stack for-stack))

              ((gosub)
               (run (find-line line-index (->int (eval-expr (cadr stmt) env arrays)))
                    (cons (+ pc 1) gosub-stack) for-stack))

              ;; A GOTO out of a subroutine (instead of RETURN) never
              ;; pops gosub-stack, so its entry sits there until some
              ;; later, unrelated RETURN pops it and jumps to an
              ;; address the program never intended -- silently, since
              ;; only the *empty*-stack case is treated as an error.
              ;; Classic BASIC has the same behavior for the same
              ;; reason (GOTO has no way to know it's leaving a
              ;; subroutine); balanced GOSUB/RETURN pairs are
              ;; unaffected.
              ((return)
               (if (null? gosub-stack)
                   (basic-error "RETURN without GOSUB")
                   (run (car gosub-stack) (cdr gosub-stack) for-stack)))

              ;; The loader (flatten-stmts) already spliced a THEN
              ;; clause's statements directly into this same vector,
              ;; immediately after this slot, and replaced 'if with
              ;; 'if-skip carrying how many of them there are. So a
              ;; true condition just falls through into them like any
              ;; other sequence of statements -- no separate execution
              ;; path, which is exactly what lets GOSUB/RETURN inside a
              ;; THEN clause work with the *same* gosub-stack as
              ;; everything else in the program.
              ((if-skip)
               (if (truthy? (eval-expr (cadr stmt) env arrays))
                   (run (+ pc 1) gosub-stack for-stack)
                   (run (+ pc 1 (caddr stmt)) gosub-stack for-stack)))

              ((for)
               (let ((name (cadr stmt)))
                 (hash-table-set! env name (eval-expr (caddr stmt) env arrays))
                 (run (+ pc 1) gosub-stack
                      (cons (list name
                                  (eval-expr (car (cdddr stmt)) env arrays)
                                  (eval-expr (car (cdddr (cdr stmt))) env arrays)
                                  (+ pc 1))
                            for-stack))))

              ((next)
               (let* ((name (or (cadr stmt) (and (pair? for-stack) (caar for-stack))))
                      (frame (and name (find-for-frame name for-stack))))
                 (if (not frame)
                     (basic-error "NEXT without matching FOR" name)
                     (let* ((limit (cadr frame)) (step (caddr frame)) (body-pc (car (cdddr frame)))
                            (v (+ (env-get env name) step)))
                       (hash-table-set! env name v)
                       (if (if (> step 0) (<= v limit) (>= v limit))
                           (run body-pc gosub-stack for-stack)
                           (run (+ pc 1) gosub-stack (remove-for-frame name for-stack)))))))

              (else (basic-error "unknown statement" stmt))))))))

;; A linear scan, not a hash-table lookup like everything else keyed
;; by name in this interpreter (env, arrays) -- loop nesting depth is
;; small in practice (unlike the number of GOTO targets a program can
;; have), so this stays O(loop-nesting-depth) rather than needing the
;; O(1) treatment the rest of the design insists on for GOTO/array
;; access.
;;
;; Known limitation shared with classic unstructured BASIC: nothing
;; here can tell "loop exited via NEXT" apart from "loop abandoned via
;; GOTO/GOSUB out of its body." A FOR whose body is left early by a
;; GOTO (a common way to `break` out of a loop) leaves its frame on
;; for-stack indefinitely; a later NEXT reusing the same variable name
;; -- or a bare NEXT, if that stale frame is now the innermost one --
;; resumes the abandoned loop instead of raising an error. Real
;; unstructured BASIC has this exact same sharp edge for the same
;; reason (there's no structured block to know the extent of), so
;; this isn't a shortcut relative to the language being modeled, but a
;; stricter dialect could reject it by tracking each frame's owning pc
;; range and popping any frame a GOTO jumps outside of.
(define (find-for-frame name for-stack)
  (cond ((null? for-stack) #f)
        ((equal? (caar for-stack) name) (car for-stack))
        (else (find-for-frame name (cdr for-stack)))))

(define (remove-for-frame name for-stack)
  (cond ((null? for-stack) '())
        ((equal? (caar for-stack) name) (cdr for-stack))
        (else (cons (car for-stack) (remove-for-frame name (cdr for-stack))))))

;;; ------------------------------------------------------------------
;;; 6. DRIVER
;;; ------------------------------------------------------------------

(define (read-all-lines path)
  (let ((port (open-input-file path)))
    (let loop ((acc '()))
      (let ((line (read-line port)))
        (if (eof-object? line)
            (begin (close-port port) (reverse acc))
            (loop (cons line acc)))))))

(define (run-basic-source source-lines)
  (let* ((loaded (load-program source-lines))
         (stmts (car loaded))
         (line-index (cadr loaded)))
    (run-program stmts line-index (make-env) (make-arrays))))

;; A demo program exercising every feature above: FOR/NEXT, GOSUB/
;; RETURN, IF/THEN, array DIM, and both variable types.
(define demo-program
  '("10 REM -- Fibonacci via FOR/NEXT, then a GOSUB-based factorial"
    "20 PRINT \"First 10 Fibonacci numbers:\""
    "30 DIM F(10)"
    "40 LET F(0) = 0 : LET F(1) = 1"
    "50 FOR I = 2 TO 10"
    "60 LET F(I) = F(I-1) + F(I-2)"
    "70 NEXT I"
    "80 FOR I = 0 TO 10"
    "90 PRINT F(I); \" \";"
    "100 NEXT I"
    "110 PRINT \"\""
    "120 PRINT \"5! computed via GOSUB:\""
    "130 LET N = 5"
    "140 GOSUB 200"
    "150 PRINT \"Result = \"; RESULT"
    "160 END"
    "200 REM -- factorial subroutine: N in, RESULT out"
    "210 LET RESULT = 1"
    "220 LET K = N"
    "230 IF K <= 1 THEN RETURN"
    "240 LET RESULT = RESULT * K"
    "250 LET K = K - 1"
    "260 GOTO 230"))

;; command-line-args' first element is this script's own path (curry's
;; equivalent of argv[0]), so a user-supplied program path is whatever
;; follows it, not the head of the list.
(let ((args (cdr command-line-args)))
  (if (pair? args)
      (run-basic-source (read-all-lines (car args)))
      (run-basic-source demo-program)))
