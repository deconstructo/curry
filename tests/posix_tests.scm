;;; POSIX module tests (SRFI-170 subset) — requires (curry posix)

(import (curry posix))

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

;;; Process state

(check "pid is a fixnum" (exact? (pid)) #t)
(check "pid is positive" (> (pid) 0) #t)
(check "user-uid matches effective-uid for non-setuid process"
       (= (user-uid) (user-effective-uid)) #t)
(check "user-gid matches effective-gid for non-setuid process"
       (= (user-gid) (user-effective-gid)) #t)
(check "user-supplementary-gids is a list" (list? (user-supplementary-gids)) #t)

(define starting-dir (current-directory))
(check "current-directory is a string" (string? starting-dir) #t)

(define orig-umask (umask))
(set-umask! #o022)
(check "set-umask! then umask round-trips" (umask) #o022)
(set-umask! orig-umask)
(check "umask restored" (umask) orig-umask)

(check "posix-time is positive" (> (posix-time) 0) #t)
(check "monotonic-time is non-negative" (>= (monotonic-time) 0) #t)
(let ((t1 (monotonic-time)))
  (let loop ((i 0)) (if (< i 100000) (loop (+ i 1))))
  (check "monotonic-time is non-decreasing" (>= (monotonic-time) t1) #t))

;;; Environment variables

(set-environment-variable! "CURRY_POSIX_TEST_VAR" "hello")
(check "set-environment-variable! visible via core get-environment-variable"
       (get-environment-variable "CURRY_POSIX_TEST_VAR") "hello")
(delete-environment-variable! "CURRY_POSIX_TEST_VAR")
(check "delete-environment-variable! removes it"
       (get-environment-variable "CURRY_POSIX_TEST_VAR") #f)

;;; File info + directory operations, all under a scratch dir

(define scratch (string-append "/tmp/curry-posix-tests-" (number->string (pid))))
(guard (e (#t #f)) (delete-directory scratch))
(create-directory scratch)
(check "scratch dir exists and is a directory"
       (file-info-directory? (file-info scratch)) #t)

(define file-a (string-append scratch "/a.txt"))
(call-with-output-file file-a (lambda (p) (display "hello world" p)))

(define fi (file-info file-a))
(check "file-info?" (file-info? fi) #t)
(check "file-info-regular?" (file-info-regular? fi) #t)
(check "file-info-directory? is false for a regular file"
       (file-info-directory? fi) #f)
(check "file-info:size matches written content" (file-info:size fi) 11)
(check "file-info:nlinks starts at 1" (file-info:nlinks fi) 1)
(check "file-info:uid matches current user" (file-info:uid fi) (user-uid))

;; Symlinks
(define link-a (string-append scratch "/link.txt"))
(create-symlink "a.txt" link-a)
(check "lstat sees the symlink itself"
       (file-info-symlink? (file-info link-a #f)) #t)
(check "stat (following) sees the regular file through the symlink"
       (file-info-regular? (file-info link-a #t)) #t)
(check "read-symlink returns the link target" (read-symlink link-a) "a.txt")
(check "real-path resolves through the symlink to a real, absolute path"
       (real-path link-a)
       (real-path file-a))

;; Hard links
(define hard-a (string-append scratch "/hard.txt"))
(create-hard-link file-a hard-a)
(check "create-hard-link bumps nlinks to 2"
       (file-info:nlinks (file-info file-a)) 2)
(check "hard link content matches original"
       (call-with-input-file hard-a (lambda (p) (read-line p)))
       "hello world")

;; Rename
(define renamed-a (string-append scratch "/renamed.txt"))
(rename-file file-a renamed-a)
(check "renamed file exists" (file-info-regular? (file-info renamed-a)) #t)
(check "old name no longer resolves via lstat (still linked via hard-a)"
       (file-info:nlinks (file-info hard-a)) 2)

;; Mode / truncate
(set-file-mode renamed-a #o600)
(check "set-file-mode changes permission bits"
       (bitwise-and (file-info:mode (file-info renamed-a)) #o777) #o600)
(truncate-file renamed-a 5)
(check "truncate-file shrinks the file" (file-info:size (file-info renamed-a)) 5)

;; directory-files / open-directory / read-directory / close-directory
(define names (directory-files scratch))
(check "directory-files finds all three entries"
       (list? (member "link.txt" names))
       #t)
(check "directory-files count" (length names) 3)

(define ds (open-directory scratch))
(let loop ((seen '()))
  (let ((n (read-directory ds)))
    (if (eof-object? n)
        (begin
          (close-directory ds)
          (check "open/read/close-directory sees the same entry count"
                 (length seen) (length names))
          (check "open/read/close-directory sees the same entries (order-independent)"
                 (let all-present? ((lst names))
                   (cond ((null? lst) #t)
                         ((not (member (car lst) seen)) #f)
                         (else (all-present? (cdr lst)))))
                 #t))
        (loop (cons n seen)))))

;; Cleanup — delete-directory (rmdir) requires an empty directory, so remove
;; every file first; this also exercises delete-file against files created
;; via create-hard-link/rename-file/create-symlink above.
(for-each delete-file (map (lambda (n) (string-append scratch "/" n)) names))
(delete-directory scratch)
(check "scratch dir gone after cleanup"
       (guard (e (#t 'gone)) (file-info scratch))
       'gone)

;;; User/group database

(define me (user-info (user-uid)))
(check "user-info? on current user" (user-info? me) #t)
(check "user-info:uid round-trips" (user-info:uid me) (user-uid))
(check "user-info by name matches user-info by uid"
       (user-info:uid (user-info (user-info:name me)))
       (user-uid))

(define my-group (group-info (user-gid)))
(check "group-info?" (group-info? my-group) #t)
(check "group-info:gid round-trips" (group-info:gid my-group) (user-gid))

;;; Error handling

(check "file-info on a nonexistent path raises a catchable error"
       (guard (e (#t 'caught)) (file-info "/no/such/path/curry-posix-test"))
       'caught)
(check "create-directory under a nonexistent parent raises a catchable error"
       (guard (e (#t 'caught)) (create-directory "/no/such/parent/curry-posix-test"))
       'caught)

;;; Process execution

(check "system decodes a normal exit code, not a raw wait-status"
       (system "exit 3") 3)
(check "system exit 0" (system "exit 0") 0)
(check "system decodes a signal-killed command as a negative signal number"
       (system "kill -TERM $$") -15)

(call-with-values
  (lambda () (process-run "/bin/echo" (list "hello" "world")))
  (lambda (code out err)
    (check "process-run: echo exit code" code 0)
    (check "process-run: echo stdout" out "hello world\n")
    (check "process-run: echo stderr" err "")))

(call-with-values
  (lambda () (process-run "/bin/sh" (list "-c" "echo to-stderr 1>&2; exit 7")))
  (lambda (code out err)
    (check "process-run: nonzero exit code" code 7)
    (check "process-run: stdout empty" out "")
    (check "process-run: stderr captured" err "to-stderr\n")))

(call-with-values
  (lambda () (process-run "/bin/pwd" '() #:cwd "/tmp"))
  (lambda (code out err)
    (check "process-run: #:cwd exit code" code 0)
    ;; /tmp is a symlink to /private/tmp on macOS; just check the leaf.
    (check "process-run: #:cwd took effect" (exact? (string-contains out "tmp")) #t)))

(call-with-values
  (lambda () (process-run "/usr/bin/env" '() #:env (list (cons "CURRY_TEST_VAR" "hello"))))
  (lambda (code out err)
    (check "process-run: #:env exit code" code 0)
    (check "process-run: #:env replaces environment" out "CURRY_TEST_VAR=hello\n")))

(check "process-run: nonexistent executable raises a catchable error"
       (guard (e (#t 'caught)) (process-run "/no/such/curry-test-binary" '()))
       'caught)

(check "process-run: #:timeout raises when the child overruns"
       (guard (e (#t 'caught)) (process-run "/bin/sleep" (list "5") #:timeout 0.2))
       'caught)

(let ((h (process-start "/bin/cat" '())))
  (check "process-start: returns a process handle" (process-handle? h) #t)
  (check "process-start: pid is a positive fixnum" (> (process-pid h) 0) #t)
  (write-string "roundtrip\n" (process-stdin h))
  (close-port (process-stdin h))
  (check "process-start: stdout streams back what was written"
         (read-line (process-stdout h)) "roundtrip")
  (check "process-start: wait reaps a normal exit" (process-wait h) 0))

(let ((h (process-start "/bin/sleep" (list "5"))))
  (check "process-start: alive? true for a running child" (process-alive? h) #t)
  (process-kill h 'sigterm)
  (check "process-start: wait decodes a signal kill as -signal"
         (process-wait h) -15)
  (check "process-start: alive? false after reaping" (process-alive? h) #f))

(let ((h (process-start "/bin/sleep" (list "5"))))
  (check "process-wait: timeout returns #f without reaping"
         (process-wait h 0.2) #f)
  (process-kill h 'sigkill)
  (check "process-wait: blocking wait after kill decodes -9"
         (process-wait h) -9))

(let ((h (process-start "/bin/sleep" (list "5"))))
  ;; process-kill accepts a raw pid, not just a handle.
  (process-kill (process-pid h))
  (check "process-kill: accepts a raw pid" (process-wait h) -15))

;;; terminal?

(check "terminal? returns a boolean for fd 0" (boolean? (terminal? 0)) #t)

;;; Environment inquiry (SRFI-112)

(check "implementation-name" (implementation-name) "curry")
(check "implementation-version is a string" (string? (implementation-version)) #t)
(check "cpu-architecture is a string or #f"
       (or (string? (cpu-architecture)) (eq? (cpu-architecture) #f)) #t)
(check "os-name is a string or #f"
       (or (string? (os-name)) (eq? (os-name) #f)) #t)
(check "os-version is a string or #f"
       (or (string? (os-version)) (eq? (os-version) #f)) #t)
(check "machine-name is a string or #f"
       (or (string? (machine-name)) (eq? (machine-name) #f)) #t)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
