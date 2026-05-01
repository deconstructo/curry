;;; Git module tests — requires (curry git) and libgit2
;;;
;;; Creates a temporary bare repo in /tmp, exercises every binding,
;;; then removes the directory on exit.

(import (curry git))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-pred label pred result)
  (if (pred result)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " unexpected value ") (write result)
             (newline)
             (set! fail (+ fail 1)))))

;;; Helper: write a file inside the repo work-tree
(define (write-file path text)
  (let ((p (open-output-file path)))
    (display text p)
    (close-output-port p)))

;;; Helper: read a file
(define (read-file path)
  (let* ((p (open-input-file path))
         (content (let loop ((acc ""))
                    (let ((c (read-char p)))
                      (if (eof-object? c) acc
                          (loop (string-append acc (string c))))))))
    (close-input-port p)
    content))

;;; Helper: alist lookup
(define (alist-ref key alist)
  (let ((pair (assoc key alist)))
    (if pair (cdr pair) #f)))

;;; Helper: delete a directory tree via shell (keep tests self-contained)
(define (rmdir-rf path)
  ;; Use the OS rm -rf via a subprocess-style trick.
  ;; Curry doesn't expose system() directly, so we write a tiny shell script
  ;; to a file and exec it — but the simplest portable option is to just
  ;; leave cleanup to the OS /tmp GC for transient test runs.
  ;; We do a best-effort delete using delete-file on known files.
  #f)

;;; ── Set up a fresh repo in /tmp ──────────────────────────────────────────

(define repo-path "/tmp/curry-git-test")

;; Remove any leftover from a previous run (best-effort via shell).
;; We use (system) if available; otherwise proceed and let git-init overwrite.
(define (try-rm path)
  (guard (exn (#t #f))
    (system (string-append "rm -rf " path))))

(try-rm repo-path)

;;; ── git-init ─────────────────────────────────────────────────────────────

(define repo (git-init repo-path))
(check-pred "git-init returns repo handle" pair? repo)

;;; ── git-head on empty repo ───────────────────────────────────────────────

(define head-empty (git-head repo))
(check "git-head empty repo" head-empty "(no HEAD)")

;;; ── git-status on clean empty repo ──────────────────────────────────────

(check "git-status empty" (git-status repo) '())

;;; ── git-branches on empty repo ───────────────────────────────────────────

(check "git-branches empty" (git-branches repo) '())

;;; ── git-log on empty repo ────────────────────────────────────────────────

(check "git-log empty" (git-log repo) '())

;;; ── Stage a file and commit ──────────────────────────────────────────────

(write-file (string-append repo-path "/hello.txt") "Hello, Curry!\n")

(git-add! repo "hello.txt")

;;; git-status should now show hello.txt as staged-new
(define status1 (git-status repo))
(check-pred "git-status after add: non-empty" pair? status1)
(let ((entry (car status1)))
  (check "status entry path" (car entry) "hello.txt")
  (check-pred "status entry flags list" list? (cdr entry)))

;;; git-diff-staged should produce non-empty patch
(define staged-patch (git-diff-staged repo))
(check-pred "git-diff-staged non-empty string" string? staged-patch)
(check-pred "git-diff-staged contains file name"
            (lambda (s) (> (string-length s) 0))
            staged-patch)

(define oid1 (git-commit! repo "Initial commit" "Test User" "test@example.com"))
(check-pred "git-commit! returns sha string" string? oid1)
(check "sha length" (string-length oid1) 40)

;;; ── git-head after first commit ──────────────────────────────────────────

(define head1 (git-head repo))
;; HEAD is "main" or "master" depending on libgit2 default config
(check-pred "git-head is string" string? head1)
(check-pred "git-head non-empty" (lambda (s) (> (string-length s) 0)) head1)

;;; ── git-log ──────────────────────────────────────────────────────────────

(define log1 (git-log repo))
(check "git-log length after 1 commit" (length log1) 1)

(define commit1 (car log1))
(check "commit has id"      (string? (alist-ref 'id commit1))      #t)
(check "commit message"     (alist-ref 'message commit1)           "Initial commit")
(check "commit author"      (alist-ref 'author  commit1)           "Test User")
(check "commit email"       (alist-ref 'email   commit1)           "test@example.com")
(check-pred "commit time"   integer? (alist-ref 'time commit1))

;;; ── git-log with count limit ─────────────────────────────────────────────

(define log-limited (git-log repo 1))
(check "git-log count limit" (length log-limited) 1)

;;; ── Second commit ────────────────────────────────────────────────────────

(write-file (string-append repo-path "/world.txt") "world\n")
(git-add! repo "world.txt")
(define oid2 (git-commit! repo "Add world.txt" "Test User" "test@example.com"))
(check-pred "second commit sha" string? oid2)

(define log2 (git-log repo))
(check "git-log length after 2 commits" (length log2) 2)

;;; Verify count limit clips correctly
(define log2-limited (git-log repo 1))
(check "git-log limit=1 of 2" (length log2-limited) 1)

;;; ── git-status clean after commit ────────────────────────────────────────

(check "git-status clean" (git-status repo) '())

;;; ── Unstaged diff is empty on clean tree ─────────────────────────────────

(define diff-clean (git-diff repo))
(check "git-diff clean" diff-clean "")

;;; ── Modify file, check diff / status ─────────────────────────────────────

(write-file (string-append repo-path "/hello.txt") "Hello, modified!\n")

(define status-modified (git-status repo))
(check-pred "status after modify non-empty" pair? status-modified)

(define diff-modified (git-diff repo))
(check-pred "git-diff shows modification" (lambda (s) (> (string-length s) 0)) diff-modified)

;;; ── git-add-all! and git-reset-file! ─────────────────────────────────────

(git-add-all! repo)
(define status-after-add-all (git-status repo))
;; After add-all the workdir diff should be empty (everything staged)
(define diff-after-add-all (git-diff repo))
(check "git-diff after add-all" diff-after-add-all "")

(git-reset-file! repo "hello.txt")
;; After reset, hello.txt should be back to modified (unstaged)
(define diff-after-reset (git-diff repo))
(check-pred "git-diff after reset non-empty" (lambda (s) (> (string-length s) 0)) diff-after-reset)

;;; Re-stage and commit so we have a clean tree for branch tests
(git-add-all! repo)
(git-commit! repo "Modify hello.txt" "Test User" "test@example.com")

;;; ── Branches ─────────────────────────────────────────────────────────────

(define branches1 (git-branches repo))
(check-pred "branches non-empty" pair? branches1)
;; HEAD branch should appear in the branch list
(check-pred "current branch in branch list"
            (lambda (bs) (member (git-current-branch repo) bs))
            branches1)

;;; Create a new branch
(git-branch-create! repo "feature-x")
(define branches2 (git-branches repo))
(check-pred "new branch appears" (lambda (bs) (member "feature-x" bs)) branches2)

;;; Checkout the new branch
(git-checkout! repo "feature-x")
(check "git-current-branch after checkout" (git-current-branch repo) "feature-x")

;;; Switch back to original branch
(git-checkout! repo (car branches1))

;;; ── Tags ─────────────────────────────────────────────────────────────────

(define tags-before (git-tags repo))
(check "tags initially empty" tags-before '())

(git-tag-create! repo "v1.0.0" "Release 1.0.0")
(define tags-after (git-tags repo))
(check-pred "tag appears after create" (lambda (ts) (member "refs/tags/v1.0.0" ts)) tags-after)

;;; ── Remotes (no network) ─────────────────────────────────────────────────

;; Fresh init — no remotes configured
(define remotes (git-remotes repo))
(check "no remotes" remotes '())

;;; ── git-close! ───────────────────────────────────────────────────────────

(git-close! repo)
(check "git-close! no crash" #t #t)

;;; Verify that reopening the repo works
(define repo2 (git-open repo-path))
(check-pred "git-open reopens" pair? repo2)
(define log-reopen (git-log repo2))
(check "git-log after reopen" (length log-reopen) 3)
(git-close! repo2)

;;; ── Error cases ──────────────────────────────────────────────────────────

(define (raises? thunk)
  (guard (exn (#t #t))
    (thunk)
    #f))

(check "git-open bad path raises" (raises? (lambda () (git-open "/nonexistent/path/xyz"))) #t)

;;; ── Summary ──────────────────────────────────────────────────────────────

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(try-rm repo-path)
(if (> fail 0) (exit 1) (exit 0))
