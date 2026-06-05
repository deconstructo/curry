# module: `(curry llm)`

LLM client library with multi-turn conversation, tool use, and a full agentic
loop. Talks to Anthropic (Claude), OpenAI, Ollama, and any OpenAI-compatible
endpoint.

## Prerequisites

`(curry llm)` is a pure Scheme library — no C module, no build step. It depends
on `(curry http)` (libcurl) and `(curry json)` (always-on). Both must be
available.

## Concepts

**Client** — holds provider identity, API endpoint, API key, and default model.
Immutable once created.

**Conversation** — mutable state: message history, registered tools, system
prompt. One conversation per "thread" of dialogue. Tied to one client (hence one
provider).

**Tool** — a Scheme lambda the LLM can call. The library runs the full agentic
loop: after receiving a tool-call response, it executes your lambda, feeds the
result back, and continues until the model stops calling tools.

## Client constructors

```scheme
(import (curry llm))

; Generic — provider is 'claude, 'openai, 'ollama, or a URL string
(make-llm-client provider [model] [api-key])

; Convenience constructors
(make-llm-client/claude   api-key [model])   ; default model: claude-opus-4-7
(make-llm-client/openai   api-key [model])   ; default model: gpt-4o
(make-llm-client/ollama   [model] [endpoint]); default: llama3.2 @ localhost:11434
(make-llm-client/openai-compat endpoint [api-key] [model])
```

API keys may also be supplied via environment variables:

| Provider | Env var |
|----------|---------|
| `'claude` | `ANTHROPIC_API_KEY` |
| `'openai` | `OPENAI_API_KEY` |
| Ollama / compat | (no key needed) |

## Conversation API

```scheme
; Create a conversation (optionally override model)
(make-conversation client [model]) -> conv

; Set a system-level instruction (call before first conv-send!)
(conv-system! conv prompt-string)

; Register a tool the LLM may call
(conv-tool! conv name description params handler)
;   name:        string — tool name, e.g. "get-weather"
;   description: string — what the tool does (shown to the model)
;   params:      list of (name type description) triples — all required
;   handler:     (lambda (args) ...) where args is alist of (symbol . value)
;                Must return a string or a value that will be JSON-encoded.

; Send a user message; returns the assistant's final text reply
(conv-send! conv message-string) -> reply-string

; Retrieve the last reply without sending anything
(conv-last-reply conv) -> string or #f

; Clear message history (keeps system prompt and tools)
(conv-clear! conv)

; Inspect the raw message history (provider-native format)
(conv-history conv) -> list
```

## Simple one-shot API

```scheme
; Ask a single question with no history or tools
(llm-ask client message-string [model]) -> reply-string
```

## Tool parameter format

`params` is a list of triples: `(name type description)`.

```scheme
'((city    "string"  "City name, e.g. London")
  (country "string"  "ISO 3166 country code")
  (units   "string"  "celsius or fahrenheit"))
```

All listed parameters are required. The `handler` lambda receives an alist of
`(symbol . value)` pairs — one per parameter. Use `assq` to extract values:

```scheme
(lambda (args)
  (let ((city (cdr (assq 'city args)))
        (units (cdr (assq 'units args))))
    (fetch-weather city units)))
```

The return value of the handler is sent back to the model as the tool result.
Strings are used as-is; other values are JSON-encoded.

## Examples

### Simple chat

```scheme
(import (curry llm))

(define client (make-llm-client/claude (getenv "ANTHROPIC_API_KEY")))
(define reply  (llm-ask client "What is the capital of France?"))
(display reply)
```

### Multi-turn conversation

```scheme
(define client (make-llm-client/ollama "llama3.2"))
(define conv   (make-conversation client))

(conv-system! conv "You are a concise assistant. Keep answers under 2 sentences.")

(display (conv-send! conv "What is a monad?"))
(newline)
(display (conv-send! conv "Give me a simple Haskell example."))
(newline)
```

### Tool use (agentic loop)

```scheme
(import (curry llm))

(define client (make-llm-client/claude (getenv "ANTHROPIC_API_KEY")))
(define conv   (make-conversation client))

(conv-system! conv "Use tools to answer questions accurately.")

(conv-tool! conv "sqrt"
  "Compute the square root of a number."
  '((n "number" "The number to compute the square root of."))
  (lambda (args)
    (number->string (sqrt (cdr (assq 'n args))))))

; The model will call sqrt(2) when answering this
(display (conv-send! conv "What is the square root of 2? Use the sqrt tool."))
(newline)
```

### Ollama (local, no API key)

```scheme
(define client (make-llm-client/ollama "mistral"))
(display (llm-ask client "Explain tail-call optimisation in one paragraph."))
```

### OpenAI-compatible endpoint (e.g. local vLLM)

```scheme
(define client
  (make-llm-client/openai-compat "http://localhost:8000/v1/chat/completions"
                                 #f           ; no key
                                 "meta-llama/Meta-Llama-3-8B-Instruct"))
(display (llm-ask client "Hello!"))
```

## Provider notes

| Provider | Protocol | System prompt | Tool use |
|----------|----------|---------------|----------|
| `'claude` | Anthropic Messages API | `system` field | Native tool_use blocks |
| `'openai` | OpenAI Chat Completions | system message | function calling |
| `'ollama` | OpenAI-compatible | system message | function calling (model-dependent) |
| URL string | OpenAI-compatible | system message | function calling |

Anthropic and OpenAI have incompatible history formats, so a conversation is
tied to the provider used at creation time. Switching providers mid-conversation
is not supported; create a new conversation instead.

## Error handling

`conv-send!` and `llm-ask` raise a Scheme error on:
- Network failure (no connectivity, DNS error)
- HTTP 4xx/5xx responses (with the provider's error message included)
- Malformed JSON in the response

Wrap calls in `guard` to handle errors gracefully:

```scheme
(define reply
  (guard (exn (#t (string-append "Error: " (error-message exn))))
    (conv-send! conv "Hello")))
```
