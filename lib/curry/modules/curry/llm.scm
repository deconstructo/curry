;;; (curry llm) — LLM client library with tool-use / agentic loop.
;;;
;;; Supported providers: claude (Anthropic), openai, ollama, openai-compat.
;;;
;;; API:
;;;   (make-llm-client provider [model] [api-key])  -> client
;;;   (make-llm-client/claude   api-key [model])    -> client
;;;   (make-llm-client/openai   api-key [model])    -> client
;;;   (make-llm-client/ollama   [model] [endpoint]) -> client
;;;   (make-llm-client/openai-compat endpoint [api-key] [model]) -> client
;;;
;;;   (make-conversation client [model]) -> conv
;;;   (conv-system! conv prompt)         -> unspecified
;;;   (conv-tool!   conv name desc params handler) -> unspecified
;;;     params: list of (name type description) triples; all required
;;;     handler: (lambda (args) ...) where args is alist of (symbol . value)
;;;   (conv-send! conv message)          -> reply-string
;;;   (conv-last-reply conv)             -> reply-string or #f
;;;   (conv-clear!    conv)              -> unspecified  (keeps tools/system)
;;;   (conv-history   conv)              -> messages list
;;;   (llm-ask client message [model])   -> reply-string

(import (curry http))
(import (curry json))

;;; ─── JSON encoder ───────────────────────────────────────────────────────────
;;; We encode our own JSON to build request bodies.
;;; Convention: alists → JSON objects, vectors → JSON arrays.

(define (j val)
  (define p (open-output-string))
  (define (j! v)
    (cond
      ((string? v)
       (write-char #\" p)
       (string-for-each
         (lambda (c)
           (cond
             ((char=? c #\") (display "\\\"" p))
             ((char=? c #\\) (display "\\\\" p))
             ((char=? c #\newline) (display "\\n" p))
             ((char=? c #\return) (display "\\r" p))
             ((char=? c #\tab) (display "\\t" p))
             (else (write-char c p))))
         v)
       (write-char #\" p))
      ((boolean? v) (display (if v "true" "false") p))
      ((null? v) (display "null" p))
      ((number? v) (display (number->string v) p))
      ((vector? v)
       (write-char #\[ p)
       (let loop ((i 0))
         (when (< i (vector-length v))
           (when (> i 0) (write-char #\, p))
           (j! (vector-ref v i))
           (loop (+ i 1))))
       (write-char #\] p))
      ((pair? v)
       (write-char #\{ p)
       (let loop ((l v) (first #t))
         (unless (null? l)
           (unless first (write-char #\, p))
           (let ((kv (car l)))
             (j! (if (symbol? (car kv)) (symbol->string (car kv)) (car kv)))
             (write-char #\: p)
             (j! (cdr kv)))
           (loop (cdr l) #f)))
       (write-char #\} p))
      (else (display "null" p))))
  (j! val)
  (get-output-string p))

;;; ─── JSON response helpers ──────────────────────────────────────────────────
;;; json-parse returns alists (string keys) for objects, vectors for arrays.

(define (jget obj key)
  (let ((p (assoc key obj)))
    (if p (cdr p) #f)))

(define (jget* obj . keys)
  (let loop ((v obj) (ks keys))
    (if (null? ks) v
        (loop (jget v (car ks)) (cdr ks)))))

(define (str-join lst sep)
  (if (null? lst) ""
      (let loop ((rest (cdr lst)) (acc (car lst)))
        (if (null? rest) acc
            (loop (cdr rest) (string-append acc sep (car rest)))))))

(define (list-find pred lst)
  (cond ((null? lst) #f)
        ((pred (car lst)) (car lst))
        (else (list-find pred (cdr lst)))))

(define (string-keys->symbols alist)
  (map (lambda (kv) (cons (string->symbol (car kv)) (cdr kv)))
       alist))

;;; ─── Client ─────────────────────────────────────────────────────────────────
;;; #(provider endpoint api-key default-model)

(define (make-llm-client provider . args)
  (let-values (((model api-key)
                (cond
                  ((= (length args) 0) (values #f #f))
                  ((= (length args) 1) (values (car args) #f))
                  (else (values (car args) (cadr args))))))
    (define ep
      (cond
        ((eq? provider 'claude) "https://api.anthropic.com/v1/messages")
        ((eq? provider 'openai) "https://api.openai.com/v1/chat/completions")
        ((eq? provider 'ollama) "http://localhost:11434/v1/chat/completions")
        ((pair? provider) (cdr provider))  ; '(openai-compat . "http://...")
        ((string? provider) provider)      ; bare endpoint string
        (else (error "llm: unknown provider" provider))))
    (define prov
      (cond
        ((eq? provider 'claude) 'claude)
        ((eq? provider 'openai) 'openai)
        ((eq? provider 'ollama) 'openai)
        ((pair? provider) 'openai)
        ((string? provider) 'openai)
        (else 'openai)))
    (define default-model
      (or model
          (cond
            ((eq? provider 'claude) "claude-opus-4-7")
            ((eq? provider 'openai) "gpt-4o")
            ((eq? provider 'ollama) "llama3.2")
            (else "default"))))
    (define key
      (or api-key
          (cond
            ((eq? prov 'claude)
             (let ((k (getenv "ANTHROPIC_API_KEY")))
               (if (and k (> (string-length k) 0)) k #f)))
            ((eq? provider 'openai)
             (let ((k (getenv "OPENAI_API_KEY")))
               (if (and k (> (string-length k) 0)) k #f)))
            (else #f))))
    (vector prov ep key default-model)))

(define (make-llm-client/claude api-key . args)
  (define model (if (null? args) "claude-opus-4-7" (car args)))
  (make-llm-client 'claude model api-key))

(define (make-llm-client/openai api-key . args)
  (define model (if (null? args) "gpt-4o" (car args)))
  (make-llm-client 'openai model api-key))

(define (make-llm-client/ollama . args)
  (define model    (if (> (length args) 0) (car args) "llama3.2"))
  (define endpoint (if (> (length args) 1) (cadr args) "http://localhost:11434/v1/chat/completions"))
  (make-llm-client endpoint model #f))

(define (make-llm-client/openai-compat endpoint . args)
  (define api-key (if (> (length args) 0) (car args) #f))
  (define model   (if (> (length args) 1) (cadr args) "default"))
  (make-llm-client endpoint model api-key))

(define (client-provider c) (vector-ref c 0))
(define (client-endpoint c) (vector-ref c 1))
(define (client-api-key c)  (vector-ref c 2))
(define (client-model c)    (vector-ref c 3))

;;; ─── Conversation ───────────────────────────────────────────────────────────
;;; #(client model system messages tools last-reply)

(define CONV-CLIENT   0)
(define CONV-MODEL    1)
(define CONV-SYSTEM   2)
(define CONV-MESSAGES 3)
(define CONV-TOOLS    4)
(define CONV-REPLY    5)

(define (make-conversation client . args)
  (define model (if (null? args) (client-model client) (car args)))
  (vector client model #f '() '() #f))

(define (conv-system! conv prompt)
  (vector-set! conv CONV-SYSTEM prompt))

(define (conv-tool! conv name description params handler)
  (define tools (vector-ref conv CONV-TOOLS))
  (vector-set! conv CONV-TOOLS
    (append tools (list (list name description params handler)))))

(define (conv-last-reply conv) (vector-ref conv CONV-REPLY))
(define (conv-history conv)    (vector-ref conv CONV-MESSAGES))

(define (conv-clear! conv)
  (vector-set! conv CONV-MESSAGES '())
  (vector-set! conv CONV-REPLY #f))

;;; ─── Tool schema builders ───────────────────────────────────────────────────
;;; params: list of (name type description) triples

(define (tool-schema-properties params)
  (map (lambda (p)
         (cons (car p)
               (list (cons "type" (cadr p))
                     (cons "description" (caddr p)))))
       params))

(define (tool-schema-required params)
  (list->vector (map (lambda (p) (car p)) params)))

;;; ─── HTTP helpers ───────────────────────────────────────────────────────────

(define (http-post! endpoint headers body-str)
  (define h (append headers
                    (list (cons "Content-Type" "application/json"))))
  (define result (http-request "POST" endpoint h body-str))
  (define status (car result))
  (define body   (cdr result))
  (when (and (>= status 400) (< status 600))
    (let* ((err (json-parse body))
           (msg (or (jget err "error")
                    (jget* err "error" "message")
                    body)))
      (error (string-append "llm: HTTP " (number->string status) " — "
                            (if (string? msg) msg (j msg))))))
  body)

;;; ─── Anthropic (Claude) provider ────────────────────────────────────────────

(define (anthropic-headers api-key)
  (append
    (if api-key (list (cons "x-api-key" api-key)) '())
    (list (cons "anthropic-version" "2023-06-01"))))

(define (anthropic-tool-def tool)
  (define params (caddr tool))
  (list (cons "name" (car tool))
        (cons "description" (cadr tool))
        (cons "input_schema"
              (list (cons "type" "object")
                    (cons "properties" (tool-schema-properties params))
                    (cons "required" (tool-schema-required params))))))

(define (anthropic-user-msg text)
  (list (cons "role" "user") (cons "content" text)))

(define (anthropic-tool-result-msg uses results)
  ;; uses: list of (id name input-alist), results: list of strings
  (list
    (cons "role" "user")
    (cons "content"
      (list->vector
        (map (lambda (use result)
               (list (cons "type" "tool_result")
                     (cons "tool_use_id" (car use))
                     (cons "content" result)))
             uses results)))))

(define (anthropic-send! conv)
  (define client  (vector-ref conv CONV-CLIENT))
  (define model   (vector-ref conv CONV-MODEL))
  (define system  (vector-ref conv CONV-SYSTEM))
  (define msgs    (vector-ref conv CONV-MESSAGES))
  (define tools   (vector-ref conv CONV-TOOLS))
  (define api-key (client-api-key client))

  (define body-alist
    (append
      (list (cons "model" model)
            (cons "max_tokens" 4096)
            (cons "messages" (list->vector msgs)))
      (if system (list (cons "system" system)) '())
      (if (null? tools)
          '()
          (list (cons "tools" (list->vector (map anthropic-tool-def tools)))))))

  (define resp-str
    (http-post! (client-endpoint client)
                (anthropic-headers api-key)
                (j body-alist)))
  (json-parse resp-str))

(define (anthropic-loop! conv text)
  ;; Add user message
  (vector-set! conv CONV-MESSAGES
    (append (vector-ref conv CONV-MESSAGES)
            (list (anthropic-user-msg text))))

  (let loop ()
    (define resp (anthropic-send! conv))
    (define content     (jget resp "content"))      ; vector
    (define stop-reason (jget resp "stop_reason"))

    ;; Store assistant turn in history
    (vector-set! conv CONV-MESSAGES
      (append (vector-ref conv CONV-MESSAGES)
              (list (list (cons "role" "assistant")
                          (cons "content" content)))))

    (define text-parts
      (let collect ((i 0) (acc '()))
        (if (= i (vector-length content)) (reverse acc)
            (let ((block (vector-ref content i)))
              (collect (+ i 1)
                       (if (equal? (jget block "type") "text")
                           (cons (jget block "text") acc)
                           acc))))))

    (if (equal? stop-reason "tool_use")
        ;; Execute tool calls and continue
        (let* ((tool-uses
                (let gather ((i 0) (acc '()))
                  (if (= i (vector-length content)) (reverse acc)
                      (let ((block (vector-ref content i)))
                        (gather (+ i 1)
                                (if (equal? (jget block "type") "tool_use")
                                    (cons (list (jget block "id")
                                                (jget block "name")
                                                (jget block "input"))
                                          acc)
                                    acc))))))
               (results
                (map (lambda (use)
                       (define tool-name (cadr use))
                       (define input     (caddr use))
                       (define tool
                         (list-find (lambda (t) (equal? (car t) tool-name))
                                    (vector-ref conv CONV-TOOLS)))
                       (if tool
                           (let* ((handler  (list-ref tool 3))
                                  (sym-args (string-keys->symbols input))
                                  (result   (handler sym-args)))
                             (if (string? result) result (j result)))
                           (string-append "Error: unknown tool " tool-name)))
                     tool-uses)))

          ;; Append tool results as a new user message
          (vector-set! conv CONV-MESSAGES
            (append (vector-ref conv CONV-MESSAGES)
                    (list (anthropic-tool-result-msg tool-uses results))))
          (loop))

        ;; Done
        (let ((reply (str-join text-parts "\n")))
          (vector-set! conv CONV-REPLY reply)
          reply))))

;;; ─── OpenAI-compatible provider ─────────────────────────────────────────────

(define (openai-headers api-key)
  (if api-key
      (list (cons "Authorization" (string-append "Bearer " api-key)))
      '()))

(define (openai-tool-def tool)
  (define params (caddr tool))
  (list (cons "type" "function")
        (cons "function"
              (list (cons "name" (car tool))
                    (cons "description" (cadr tool))
                    (cons "parameters"
                          (list (cons "type" "object")
                                (cons "properties" (tool-schema-properties params))
                                (cons "required" (tool-schema-required params))))))))

(define (openai-user-msg text)
  (list (cons "role" "user") (cons "content" text)))

(define (openai-send! conv)
  (define client  (vector-ref conv CONV-CLIENT))
  (define model   (vector-ref conv CONV-MODEL))
  (define system  (vector-ref conv CONV-SYSTEM))
  (define msgs    (vector-ref conv CONV-MESSAGES))
  (define tools   (vector-ref conv CONV-TOOLS))
  (define api-key (client-api-key client))

  (define all-msgs
    (if system
        (cons (list (cons "role" "system") (cons "content" system)) msgs)
        msgs))

  (define body-alist
    (append
      (list (cons "model" model)
            (cons "messages" (list->vector all-msgs)))
      (if (null? tools)
          '()
          (list (cons "tools" (list->vector (map openai-tool-def tools)))))))

  (define resp-str
    (http-post! (client-endpoint client)
                (openai-headers api-key)
                (j body-alist)))
  (json-parse resp-str))

(define (openai-loop! conv text)
  ;; Add user message
  (vector-set! conv CONV-MESSAGES
    (append (vector-ref conv CONV-MESSAGES)
            (list (openai-user-msg text))))

  (let loop ()
    (define resp          (openai-send! conv))
    (define choices       (jget resp "choices"))
    (define first-choice  (vector-ref choices 0))
    (define message       (jget first-choice "message"))
    (define finish-reason (jget first-choice "finish_reason"))
    (define content       (jget message "content"))
    (define tool-calls    (jget message "tool_calls"))

    ;; Store assistant message
    (define stored-msg
      (append (list (cons "role" "assistant")
                    (cons "content" (or content "")))
              (if tool-calls
                  (list (cons "tool_calls" tool-calls))
                  '())))
    (vector-set! conv CONV-MESSAGES
      (append (vector-ref conv CONV-MESSAGES)
              (list stored-msg)))

    (if (equal? finish-reason "tool_calls")
        (begin
          ;; Execute each tool call and append results
          (let loop2 ((i 0))
            (when (< i (vector-length tool-calls))
              (define tc      (vector-ref tool-calls i))
              (define tc-id   (jget tc "id"))
              (define fn-obj  (jget tc "function"))
              (define fn-name (jget fn-obj "name"))
              (define fn-args (jget fn-obj "arguments"))  ; JSON string
              (define parsed-args
                (if (string? fn-args) (json-parse fn-args) '()))
              (define tool
                (list-find (lambda (t) (equal? (car t) fn-name))
                           (vector-ref conv CONV-TOOLS)))
              (define result
                (if tool
                    (let* ((handler  (list-ref tool 3))
                           (sym-args (string-keys->symbols parsed-args))
                           (r        (handler sym-args)))
                      (if (string? r) r (j r)))
                    (string-append "Error: unknown tool " fn-name)))
              (vector-set! conv CONV-MESSAGES
                (append (vector-ref conv CONV-MESSAGES)
                        (list (list (cons "role" "tool")
                                    (cons "tool_call_id" tc-id)
                                    (cons "content" result)))))
              (loop2 (+ i 1))))
          (loop))

        ;; Done
        (let ((reply (or content "")))
          (vector-set! conv CONV-REPLY reply)
          reply))))

;;; ─── Dispatch ───────────────────────────────────────────────────────────────

(define (conv-send! conv message)
  (define client (vector-ref conv CONV-CLIENT))
  (if (eq? (client-provider client) 'claude)
      (anthropic-loop! conv message)
      (openai-loop!    conv message)))

;;; ─── Simple one-shot API ────────────────────────────────────────────────────

(define (llm-ask client message . args)
  (define model (if (null? args) (client-model client) (car args)))
  (define conv  (make-conversation client model))
  (conv-send! conv message))
