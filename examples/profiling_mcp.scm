;;; profiling_mcp.scm — expose Curry's runtime profiler as MCP tools.
;;;
;;; Usage (stdio, one session):
;;;   ./build/curry examples/profiling_mcp.scm
;;;
;;; Usage (SSE, persistent):
;;;   ./build/curry -e '(import (curry mcp)) (mcp-serve-sse 8081)' \
;;;                    examples/profiling_mcp.scm
;;;
;;; Claude Code config (stdio):
;;;   { "mcpServers": { "curry-profiler": {
;;;       "command": "/path/to/build/curry",
;;;       "args":    ["/path/to/examples/profiling_mcp.scm"] } } }

(import (curry profiling))
(import (curry mcp))

(mcp-tool "profiler-start"
  "Enable the Curry runtime profiler.  Level 1 = call counts, 2 = timing, 3 = primitives too."
  '((level . ((type . "integer") (description . "Profiling level 1-3") (default . 1))))
  (lambda (args)
    (let ((level (let ((p (assq 'level args))) (if p (cdr p) 1))))
      (profiler-start level)
      (mcp-text (string-append "Profiling started at level "
                               (number->string (profiler-level)))))))

(mcp-tool "profiler-stop"
  "Disable the Curry runtime profiler."
  '()
  (lambda (args)
    (profiler-stop)
    (mcp-text "Profiling stopped.")))

(mcp-tool "profiler-reset"
  "Clear all accumulated profiling data."
  '()
  (lambda (args)
    (profiler-reset)
    (mcp-text "Profiling data cleared.")))

(mcp-tool "profiler-report"
  "Return the profiling report as a formatted table of (function calls ns-total)."
  '()
  (lambda (args)
    (let ((data (profiler-report)))
      (if (null? data)
          (mcp-text "No profiling data. Call profiler-start first.")
          (let loop ((rows data) (out "function                                     calls      ns-total\n"))
            (if (null? rows)
                (mcp-text out)
                (let* ((entry  (car rows))
                       (name   (symbol->string (car entry)))
                       (calls  (number->string (cadr entry)))
                       (ns     (number->string (cddr entry)))
                       (line   (string-append
                                  (substring (string-append name "                                        ") 0 44)
                                  (substring (string-append "          " calls) (- (string-length calls)) (string-length calls))
                                  "  " ns "\n")))
                  (loop (cdr rows) (string-append out line)))))))))

(mcp-serve "curry-profiler" "1.0")
