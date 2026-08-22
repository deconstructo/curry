# Using LLMs from Curry

Curry can talk to any LLM — Claude, GPT-4o, a local Ollama model, or anything
with an OpenAI-compatible endpoint — through two modules: `(curry http)` for
raw HTTP and `(curry llm)` for the full conversation and tool-use abstraction.

This guide shows progressively more interesting things you can do with it.

---

## 1. One-liner questions

```scheme
(import (curry llm))

(define client (make-llm-client 'ollama "llama3.1"))
(display (llm-ask client "What is the Collatz conjecture?"))
```

`llm-ask` creates a throwaway conversation, sends one message, and returns the
reply. No history, no tools. Perfect for scripting.

```scheme
; Claude (reads ANTHROPIC_API_KEY from env)
(define client (make-llm-client 'claude))
(display (llm-ask client "Explain continuations in one paragraph."))

; OpenAI (reads OPENAI_API_KEY from env)
(define client (make-llm-client 'openai "gpt-4o"))
(display (llm-ask client "What is a monad?"))
```

---

## 2. Conversations with memory

`make-conversation` gives you a stateful object that remembers everything said.

```scheme
(import (curry llm))

(define c    (make-llm-client 'ollama "llama3.1"))
(define conv (make-conversation c))

(conv-system! conv
  "You are a Socratic tutor. Never give answers directly — only ask questions
   that guide the student to discover them. Stay in character.")

(let loop ()
  (display "You: ")
  (flush-output-port (current-output-port))
  (let ((line (read-line)))
    (unless (or (eof-object? line) (string=? line "quit"))
      (display "Tutor: ")
      (display (conv-send! conv line))
      (newline)
      (loop))))
```

The model remembers every exchange. Ask it about induction, then ask a follow-up
"why does the base case matter?" — it knows the context.

---

## 3. Tools: giving the model real capabilities

Tools are Scheme lambdas the LLM can invoke. The library runs the agentic loop
automatically: send → get tool call → run lambda → feed result back → repeat
until the model produces a final text reply.

### Calculator that actually computes

```scheme
(import (curry llm))

(define c    (make-llm-client 'ollama "llama3.1"))
(define conv (make-conversation c))

(conv-tool! conv "calculate"
  "Evaluate a Scheme arithmetic expression exactly."
  '((expr "string" "A Scheme expression, e.g. (expt 2 64) or (/ 355 113)"))
  (lambda (args)
    (let ((e (cdr (assq 'expr args))))
      (guard (exn (#t (string-append "Error: " (error-message exn))))
        (number->string
          (eval (read (open-input-string e))))))))

(display (conv-send! conv "What is 2^64? Use the calculate tool."))
(newline)
(display (conv-send! conv "And 2^128?"))  ; model remembers context
(newline)
```

The model knows it cannot do large exponentiation in its head, so it calls
`calculate`. The result comes back exact because Curry's numeric tower handles
bignums natively.

### Live filesystem access

```scheme
(import (curry llm))

(define c    (make-llm-client 'claude))
(define conv (make-conversation c))

(conv-system! conv "You help programmers understand their codebase.")

(conv-tool! conv "read-file"
  "Read the contents of a file."
  '((path "string" "Absolute or relative path to the file"))
  (lambda (args)
    (let ((path (cdr (assq 'path args))))
      (guard (exn (#t (string-append "Error reading " path ": " (error-message exn))))
        (call-with-input-file path
          (lambda (port)
            (let loop ((acc '()))
              (let ((line (read-line port)))
                (if (eof-object? line)
                    (apply string-append (reverse acc))
                    (loop (cons (string-append line "\n") acc)))))))))))

(conv-tool! conv "list-files"
  "List files in a directory."
  '((dir "string" "Directory path"))
  (lambda (args)
    (let ((dir (cdr (assq 'dir args))))
      (guard (exn (#t (error-message exn)))
        (apply string-append
          (map (lambda (f) (string-append f "\n"))
               (directory-files dir)))))))

; Now the model can explore your code
(display
  (conv-send! conv
    "Read examples/llm_chat.scm and tell me how the tool-use loop works."))
(newline)
```

Claude will read the file and give you a real explanation of your own code.

### Database queries

```scheme
(import (curry llm))
(import (curry sqlite))

(define db   (sqlite-open "mydata.db"))
(define c    (make-llm-client 'claude))
(define conv (make-conversation c))

(conv-system! conv
  "You are a data analyst. Use the query tool to answer questions about the
   database. The database contains tables: orders(id, customer, amount, date),
   products(id, name, price, category).")

(conv-tool! conv "query"
  "Run a SQL SELECT query on the database."
  '((sql "string" "A SELECT statement"))
  (lambda (args)
    (let ((sql (cdr (assq 'sql args))))
      (guard (exn (#t (string-append "SQL Error: " (error-message exn))))
        (let ((rows (sqlite-query db sql)))
          (if (null? rows)
              "No rows returned."
              (apply string-append
                (map (lambda (row)
                       (string-append
                         (apply string-append
                           (map (lambda (cell)
                                  (string-append (if (string? cell) cell
                                                     (if cell (number->string cell) "NULL"))
                                                 "\t"))
                                row))
                         "\n"))
                     rows))))))))

(display
  (conv-send! conv "Which product category has the highest average order value?"))
(newline)
```

The model writes the SQL, you get the answer — it can follow up, refine queries,
and compute across multiple calls.

---

## 4. Structured output

Sometimes you want the model to return data you can work with in code, not prose.
Use a tool whose "result" is actually an instruction:

```scheme
(import (curry llm))
(import (curry json))

(define c (make-llm-client 'claude))

; Ask the model to extract structured data via a tool call
(define (extract-entities text)
  (define conv  (make-conversation c))
  (define result #f)

  (conv-system! conv
    "Extract named entities from text and call the record-entities tool with them.
     Always call the tool — never reply with prose.")

  (conv-tool! conv "record-entities"
    "Record the extracted entities."
    '((people  "string" "JSON array of person names")
      (places  "string" "JSON array of place names")
      (orgs    "string" "JSON array of organisation names"))
    (lambda (args)
      (set! result
        (list (cons 'people (json-parse (cdr (assq 'people args))))
              (cons 'orgs   (json-parse (cdr (assq 'orgs args))))
              (cons 'places (json-parse (cdr (assq 'places args))))))
      "done"))

  (conv-send! conv (string-append "Extract entities from: " text))
  result)

(define entities
  (extract-entities
    "Elon Musk's SpaceX launched a rocket from Boca Chica, Texas for NASA."))

(display "People: ")
(display (map (lambda (v) (vector-ref v 0))   ; vector→list for display
              (vector->list (cdr (assq 'people entities)))))
(newline)
```

The tool acts as a typed output channel. The model is forced to call it with
structured data instead of generating prose.

---

## 5. Streaming pipelines with actors

Combine the actor system with LLM calls to process data concurrently.

`(receive)` reads from the *calling actor's own* mailbox — called from the
top-level/script thread (which isn't itself an actor), it returns `#f`
immediately instead of blocking, since there's no actor mailbox to read
from. So the top-level thread can't collect results by calling `(receive)`
once per worker the way the old version of this example did; that pattern
silently discarded every summary and returned a list of `#f`s. Workers also
can't `send!` a result back to "the caller," because the caller isn't an
actor either and so has no mailbox to send to.

The fix is the same shared-state idiom curry's own actor-ring benchmarks
use: a shared vector of results plus a mutex/condvar pair from `(curry
sync)`, so the top-level thread can block until every worker has reported
in. A vector works here — and a plain variable wouldn't — because `spawn`
deep-copies each captured variable into the new actor's own closure, so a
worker's mutation of a captured *variable* would only ever be visible to
that worker; a vector is a heap object captured *by reference*, so
`vector-set!`/`vector-ref` through it is genuinely shared across the
actor/caller boundary.

```scheme
(import (curry llm))
(import (curry sync))

(define client (make-llm-client 'ollama "llama3.1"))

; Summarise a list of texts in parallel — one actor per item, results
; collected into a shared vector and a "workers remaining" counter (also a
; vector, for the same by-reference-sharing reason) guarded by a mutex.
(define (summarise-all texts)
  (let* ((n         (length texts))
         (results   (make-vector n #f))
         (remaining (vector n))
         (done-mtx  (make-mutex))
         (done-cv   (make-condvar)))
    (let loop ((i 0) (ts texts))
      (when (pair? ts)
        (let ((idx i) (text (car ts)))
          (spawn
            (lambda ()
              (let ((summary (llm-ask client
                               (string-append
                                 "Summarise in one sentence: " text))))
                (mutex-lock! done-mtx)
                (vector-set! results idx summary)
                (vector-set! remaining 0 (- (vector-ref remaining 0) 1))
                (when (= (vector-ref remaining 0) 0)
                  (cond-signal! done-cv))
                (mutex-unlock! done-mtx)))))
        (loop (+ i 1) (cdr ts))))
    (mutex-lock! done-mtx)
    (let wait ()
      (unless (= (vector-ref remaining 0) 0)
        (cond-wait! done-cv done-mtx)
        (wait)))
    (mutex-unlock! done-mtx)
    (vector->list results)))

(define summaries
  (summarise-all
    '("The Riemann hypothesis states that all non-trivial zeros of the zeta function lie on the critical line Re(s) = 1/2."
      "The P vs NP problem asks whether every problem whose solution can be quickly verified can also be quickly solved."
      "Gödel's incompleteness theorems show that any sufficiently powerful formal system contains true statements it cannot prove.")))

(for-each (lambda (s) (display s) (newline)) summaries)
```

Each actor runs in its own thread and makes its own HTTP request. For Ollama
with a fast model, multiple requests complete roughly in parallel.

---

## 6. LLM as a Curry REPL assistant

Give the model access to Curry's own evaluator. It can write and test code:

```scheme
(import (curry llm))

(define c    (make-llm-client 'claude))
(define conv (make-conversation c))

(conv-system! conv
  "You are an expert Curry Scheme programmer. When asked to write code, use the
   eval-scheme tool to test it before showing it to the user. Fix any errors
   you encounter and try again. Only show code that actually runs correctly.")

(conv-tool! conv "eval-scheme"
  "Evaluate a Curry Scheme expression and return its output."
  '((code "string" "The Scheme expression or definitions to evaluate"))
  (lambda (args)
    (let ((code (cdr (assq 'code args))))
      (guard (exn (#t (string-append "Error: " (error-message exn))))
        (let* ((port (open-input-string code))
               (expr (read port))
               (val  (eval expr)))
          (if (eq? val (if #f #f))  ; void
              "(ok)"
              (let ((out (open-output-string)))
                (write val out)
                (get-output-string out))))))))

; Ask it to write something non-trivial
(display
  (conv-send! conv
    "Write me a function that computes the continued-fraction representation
     of a rational number, then test it on 355/113."))
(newline)
```

Claude will write code, test it, fix bugs, and only show you working output.

---

## 7. Building an MCP server backed by an LLM

Expose LLM reasoning as an MCP tool that Claude Code can call:

```scheme
(import (curry mcp))
(import (curry llm))

(define client (make-llm-client 'ollama "llama3.1"))

(mcp-tool "explain-code"
  "Explain what a piece of code does in plain English."
  '((code        . ((type . "string") (description . "The code to explain")))
    (language    . ((type . "string") (description . "Programming language") (default . "unknown"))))
  (lambda (args)
    (define code (cdr (assq 'code args)))
    (define lang (cdr (assq 'language args)))
    (mcp-text
      (llm-ask client
        (string-append "Explain this " lang " code in plain English:\n\n" code)))))

(mcp-tool "suggest-refactor"
  "Suggest improvements to a piece of code."
  '((code . ((type . "string") (description . "The code to improve"))))
  (lambda (args)
    (mcp-text
      (llm-ask client
        (string-append
          "Suggest concrete refactoring improvements for this code. "
          "Be specific and brief:\n\n"
          (cdr (assq 'code args)))))))

(display "[mcp] LLM assistant listening on port 8080\n")
(mcp-serve-sse 8080 "curry-llm-assistant" "0.8.22")
```

Add it to Claude Code's config:
```json
{ "mcpServers": { "curry-llm": { "url": "http://localhost:8080/sse" } } }
```

Now Claude Code can ask a local Ollama model to explain or refactor code as part
of its own reasoning.

---

## 8. Using the symbolic CAS as a tool

Connect the LLM to Curry's built-in computer algebra system:

```scheme
(import (curry llm))

(define c    (make-llm-client 'claude))
(define conv (make-conversation c))

(conv-system! conv
  "You are a mathematics tutor. Use the CAS tools to compute exact symbolic
   answers. Show intermediate steps in your explanation.")

(conv-tool! conv "differentiate"
  "Differentiate a symbolic expression with respect to a variable."
  '((expr "string" "Scheme expression using sym-var, e.g. (* x x)")
    (var  "string" "Variable name to differentiate with respect to"))
  (lambda (args)
    (let* ((var-name (string->symbol (cdr (assq 'var args))))
           (v        (sym-var var-name))
           (env      (list (cons var-name v)))
           (expr     (eval (read (open-input-string (cdr (assq 'expr args))))
                           env))
           (result   (∂ expr v)))
      (sym->infix result))))

(conv-tool! conv "integrate"
  "Find the antiderivative of a symbolic expression."
  '((expr "string" "Scheme expression, e.g. (* 3 (* x x))")
    (var  "string" "Variable name"))
  (lambda (args)
    (let* ((var-name (string->symbol (cdr (assq 'var args))))
           (v        (sym-var var-name))
           (env      (list (cons var-name v)))
           (expr     (eval (read (open-input-string (cdr (assq 'expr args))))
                           env))
           (result   (∫ expr v)))
      (sym->infix result))))

(display
  (conv-send! conv
    "Find the derivative of x³ + 2x² - 5x + 7, then integrate the result
     and verify you get back the original (up to a constant)."))
(newline)
```

---

## 9. Routing between providers

Pick the cheapest/fastest model for easy questions, escalate for hard ones:

```scheme
(import (curry llm))

(define fast   (make-llm-client/ollama "llama3.1"))
(define strong (make-llm-client 'claude "claude-opus-4-7"))

(define (smart-ask question)
  ; Try local model first with a short timeout heuristic:
  ; if it sounds uncertain, escalate to Claude.
  (define fast-reply (llm-ask fast question))
  (if (or (string-contains fast-reply "I'm not sure")
          (string-contains fast-reply "I don't know")
          (string-contains fast-reply "I cannot"))
      (begin
        (display "[escalating to Claude]\n")
        (llm-ask strong question))
      fast-reply))

(display (smart-ask "What is the capital of France?"))    (newline)
(display (smart-ask "Prove the Riemann hypothesis."))     (newline)
```

---

## 10. Generating and running Curry scripts

Have the model write code that you then actually execute:

```scheme
(import (curry llm))

(define c (make-llm-client 'claude))

(define (generate-and-run description)
  (define code
    (llm-ask c
      (string-append
        "Write a self-contained Curry Scheme expression (no imports needed, "
        "use only built-in procedures) that: " description
        "\n\nReturn ONLY the Scheme expression, nothing else. No markdown.")))

  (display "Generated:\n") (display code) (newline)
  (display "Result:\n")

  (guard (exn (#t (display "Error: ") (display (error-message exn)) (newline)))
    (display
      (eval (read (open-input-string code))))))

(generate-and-run "generates the first 20 Fibonacci numbers as a list")
(newline)
(generate-and-run "computes the prime factorisation of 2025")
(newline)
```

---

## Tips

**Keep conversations focused.** A conversation with a clear system prompt and
tight tools beats a vague general-purpose one. The model can't do what it doesn't
know is possible.

**Tool results are strings.** Return human-readable text (not raw data structures)
unless the model explicitly asked for JSON. The model reads your result just like
a human would.

**Handle errors in tool handlers.** Wrap the body of every handler in `guard`.
If your tool crashes, the model gets an error message and can decide how to
proceed — often it will retry with a corrected call.

**Ollama for development, cloud for production.** `llama3.1` on Ollama has no
API cost and no latency from network round-trips to a remote server. Swap the
client to Claude or GPT-4o for final quality.

**Temperature and max-tokens are not yet exposed** as Scheme parameters —
`max_tokens` is hardcoded to 4096 for Claude. If you need control over these,
file an issue or send a raw `http-request` directly to the provider's API.
