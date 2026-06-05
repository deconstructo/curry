#!/usr/bin/env curry
;;; llm_chat.scm — interactive LLM conversation demo with tool use.
;;;
;;; Usage:
;;;   curry examples/llm_chat.scm                      # Ollama (local)
;;;   ANTHROPIC_API_KEY=sk-... curry examples/llm_chat.scm claude
;;;   OPENAI_API_KEY=sk-...    curry examples/llm_chat.scm openai
;;;
;;; The demo registers two tools: get-time and calculate.
;;; Type "quit" or press Ctrl-D to exit.

(import (curry llm))

;;; ─── Tool handlers ───────────────────────────────────────────────────────────

(define (tool-get-time args)
  (define zone (let ((p (assq 'timezone args)))
                 (if p (cdr p) "UTC")))
  (string-append "Current time in " zone ": 2026-06-05T14:00:00Z  (demo value)"))

(define (tool-calculate args)
  (define expr (cdr (assq 'expression args)))
  (define result
    (guard (exn (#t "error"))
      (number->string (eval (read (open-input-string expr))))))
  (string-append expr " = " result))

;;; ─── Build client from command-line arg ─────────────────────────────────────

(define provider-arg
  (if (and (pair? command-line-args)
           (> (string-length (symbol->string (car command-line-args))) 0))
      (symbol->string (car command-line-args))
      "ollama"))

(define client
  (cond
    ((string=? provider-arg "claude")
     (display "[llm] Using Anthropic Claude\n")
     (make-llm-client 'claude))
    ((string=? provider-arg "openai")
     (display "[llm] Using OpenAI\n")
     (make-llm-client 'openai))
    (else
     (display "[llm] Using Ollama (local)\n")
     (make-llm-client 'ollama))))

;;; ─── Set up conversation ─────────────────────────────────────────────────────

(define conv (make-conversation client))

(conv-system! conv
  "You are a helpful assistant. You have access to tools: get-time returns the current time, and calculate evaluates a Scheme arithmetic expression. Use them when relevant.")

(conv-tool! conv
  "get-time"
  "Get the current time in a given timezone."
  '((timezone "string" "IANA timezone name, e.g. UTC, America/New_York"))
  tool-get-time)

(conv-tool! conv
  "calculate"
  "Evaluate a Scheme arithmetic expression and return the result."
  '((expression "string" "A Scheme expression, e.g. (* 6 7) or (sqrt 144)"))
  tool-calculate)

;;; ─── REPL loop ───────────────────────────────────────────────────────────────

(display "Chat with the LLM. Type 'quit' to exit.\n")
(display (string-append "Model: " (vector-ref client 3) "\n"))
(display "─────────────────────────────────────\n")

(let loop ()
  (display "You: ")
  (flush-output-port (current-output-port))
  (define line (read-line))
  (unless (or (eof-object? line)
              (string=? line "quit")
              (string=? line "exit"))
    (when (> (string-length line) 0)
      (display "AI:  ")
      (flush-output-port (current-output-port))
      (define reply
        (guard (exn (#t (string-append "[error] " (error-message exn))))
          (conv-send! conv line)))
      (display reply)
      (newline))
    (loop)))

(display "\nGoodbye.\n")
