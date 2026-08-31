;;; disassemble_tests.scm — (disassemble proc) and the underlying chunk_disasm
;;; fix for OP_CLOSURE's variable-length upvalue table.
;;;
;;; Regression coverage for a real bug found while wiring chunk.c's existing
;;; internal disassembler (previously stderr-only, used by
;;; compiler_ir_checks.c) up to a Scheme-callable `disassemble` builtin:
;;; disasm_one's OP_CLOSURE case consumed only the 1-byte chunk-constant
;;; index and never skipped the upval_count*2 trailing bytes vm.c's real
;;; OP_CLOSURE handler reads (an (is_local, idx) pair per captured
;;; upvalue) -- for any closure capturing at least one upvalue (the common
;;; case), every instruction disassembled after that point decoded from the
;;; wrong byte offset.

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (contains? s substr)
  (let ((slen (string-length s)) (sublen (string-length substr)))
    (let loop ((i 0))
      (cond ((> (+ i sublen) slen) #f)
            ((string=? (substring s i (+ i sublen)) substr) #t)
            (else (loop (+ i 1)))))))

;; A plain compiled procedure with no upvalues disassembles cleanly.
(define (fact n) (if (< n 2) 1 (* n (fact (- n 1)))))
(let ((asm (disassemble fact)))
  (check "fact: disassemble returns a string" (string? asm) #t)
  (check "fact: mentions RETURN" (contains? asm "RETURN") #t))

;; A single closure with exactly one upvalue -- the direct regression case:
;; before the fix, the RETURN opcode byte immediately after CLOSURE's real
;; encoding was consumed as part of a misparsed "operand" instead, and
;; never appeared as its own decoded instruction.
(define (make-adder n) (lambda (x) (+ x n)))
(let ((asm (disassemble make-adder)))
  (check "make-adder: mentions CLOSURE" (contains? asm "CLOSURE") #t)
  (check "make-adder: mentions upvals" (contains? asm "upvals") #t)
  (check "make-adder: RETURN survives past the closure's upvalue table"
         (contains? asm "RETURN") #t))

;; Two adjacent closures, each with their own upvalue table, followed by
;; more real instructions (TAIL_CALL_GLOBAL to `list`, CLOSE_UP, SLIDE,
;; RETURN) -- the strongest test that byte-offset tracking stays correct
;; across multiple variable-length OP_CLOSURE instructions in sequence,
;; not just one followed immediately by the chunk's end.
(define (counter)
  (let ((n 0))
    (list (lambda () (set! n (+ n 1)) n)
          (lambda () n))))
(let ((asm (disassemble counter)))
  (check "counter: mentions two CLOSURE upvals sections"
         (contains? asm "upvals: (local 0)") #t)
  (check "counter: mentions TAIL_CALL_GLOBAL to list"
         (contains? asm "list") #t)
  (check "counter: mentions CLOSE_UP" (contains? asm "CLOSE_UP") #t)
  (check "counter: RETURN survives past both closures' upvalue tables"
         (contains? asm "RETURN") #t))

;; Error paths.
(check "disassemble: primitive procedure raises"
       (guard (e (#t 'raised)) (disassemble car))
       'raised)
(check "disassemble: non-procedure raises"
       (guard (e (#t 'raised)) (disassemble 42))
       'raised)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
