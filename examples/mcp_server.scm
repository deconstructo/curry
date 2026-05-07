;;; examples/mcp_server.scm — Curry as a high-performance MCP server
;;;
;;; Usage:
;;;   ./build/curry examples/mcp_server.scm
;;;
;;; Add to Claude Code (~/.claude.json or project .claude/settings.json):
;;;   {
;;;     "mcpServers": {
;;;       "curry": {
;;;         "command": "/path/to/build/curry",
;;;         "args": ["/path/to/examples/mcp_server.scm"]
;;;       }
;;;     }
;;;   }
;;;
;;; Tools exposed to the client:
;;;   eval          — evaluate Scheme in the running image (state persists!)
;;;   factorial     — arbitrary-precision n! via Curry's bignum tower
;;;   define        — bind a name in the global environment
;;;
;;; Resources:
;;;   env://bindings — snapshot of current top-level binding names

(import (curry mcp))

;;; Look up an argument by name; error if absent.
(define (arg args name)
  (let ((pair (assq name args)))
    (if pair (cdr pair) (error "missing required argument" name))))

;;; Optional argument with a default value.
(define (arg/default args name default)
  (let ((pair (assq name args)))
    (if pair (cdr pair) default)))

;;; Format any value as a string for returning as mcp-text.
(define (->string val)
  (let ((port (open-output-string)))
    (write val port)
    (get-output-string port)))


;;; Tool: eval
;;; Evaluates arbitrary Scheme in the running image.
;;; State (defines, loaded modules, side-effects) persists across calls —
;;; this is the whole point.
(mcp-tool "eval"
  "Evaluate a Scheme expression in the live Curry image and return the result"
  '((expression . ((type . "string")
                   (description . "Scheme expression to evaluate"))))
  (lambda (args)
    (let* ((src  (arg args 'expression))
           (expr (read (open-input-string src)))
           (val  (eval expr)))
      (mcp-text (->string val)))))


;;; Tool: factorial
;;; Demonstrates Curry's arbitrary-precision integer tower.
;;; (factorial 1000) returns the exact integer — no floating-point loss.
(mcp-tool "factorial"
  "Compute n! exactly using Curry's arbitrary-precision integers"
  '((n . ((type . "integer")
          (description . "Non-negative integer"))))
  (lambda (args)
    (define (fact n acc)
      (if (< n 2) acc (fact (- n 1) (* n acc))))
    (let ((n (arg args 'n)))
      (when (< n 0) (error "factorial: n must be non-negative"))
      (mcp-text (number->string (fact n 1))))))


;;; Tool: define
;;; Binds a name in the global environment.  Subsequent eval calls can use it.
;;; Example: (mcp-call "define" '((name . "fib") (expression . "(lambda (n) ...)")))
(mcp-tool "define"
  "Bind a name in the global environment (persists for the lifetime of the server)"
  '((name       . ((type . "string") (description . "Symbol name to bind")))
    (expression . ((type . "string") (description . "Scheme expression for the value"))))
  (lambda (args)
    (let* ((name (string->symbol (arg args 'name)))
           (expr (read (open-input-string (arg args 'expression))))
           (val  (eval expr)))
      (eval (list 'define name (list 'quote val)))
      (mcp-text (string-append (symbol->string name) " defined")))))


;;; Tool: count-to  (progress notification demo)
;;; Counts from 1 to n, emitting a progress notification at each step.
;;; Clients that support $/progress will show a live progress bar.
(mcp-tool "count-to"
  "Count from 1 to n, emitting a progress notification at each step"
  '((n . ((type . "integer") (description . "Count target (keep small for demos)"))))
  (lambda (args)
    (let ((n (arg args 'n)))
      (let loop ((i 1))
        (when (<= i n)
          (mcp-notify-progress i n (string-append "step " (number->string i) " of " (number->string n)))
          (loop (+ i 1))))
      (mcp-text (string-append "Done — counted to " (number->string n))))))


;;; Resource: env://bindings
;;; Returns a newline-separated list of all currently-defined top-level names.
;;; Useful for Claude Code to discover what's in the running image.
(mcp-resource "env://bindings"
  "All top-level names currently defined in the running Curry image"
  (lambda (uri)
    (mcp-text
      (let ((port (open-output-string)))
        (for-each (lambda (name)
                    (display name port)
                    (newline port))
                  (environment-bindings (the-environment)))
        (get-output-string port)))))


;;; Start the server.  This blocks until stdin closes (i.e. the client disconnects).
(mcp-serve "curry" "0.1.7")
