# Module: (curry git)

Git repository access via libgit2.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libgit2-dev

# macOS
brew install libgit2
```

Enable: `-DBUILD_MODULE_GIT=ON` (default on).

## Import

```scheme
(import (curry git))
```

## Repository

```scheme
(git-open  path)         ; → repo  (open existing repository)
(git-init  path)         ; → repo  (initialise new repository)
(git-clone url dest)     ; → repo  (clone from URL)
(git-close! repo)        ; → void
```

## Status

```scheme
(git-status repo)
; → ((path . (flag ...)) ...)
; Flags: staged-new  staged-modified  staged-deleted
;        untracked  modified  deleted  conflicted

(git-head repo)          ; → string  (current branch name or SHA)
```

## Log

```scheme
(git-log repo)           ; → list of commit alists (up to 100)
(git-log repo count)     ; → list of commit alists (limited)
```

Each commit alist:

```
((id      . "abc123...")
 (message . "Fix the thing")
 (author  . "Alice")
 (email   . "alice@example.com")
 (time    . 1700000000))   ; Unix timestamp
```

## Staging area

```scheme
(git-add!       repo path)   ; stage a file
(git-add-all!   repo)        ; stage all changes (git add -A)
(git-reset-file! repo path)  ; unstage a file
```

## Committing

```scheme
(git-commit! repo message author-name author-email)
; → string (SHA of new commit)
```

The index must be populated with `git-add!` before committing.

## Branches

```scheme
(git-branches       repo)          ; → list of branch name strings
(git-current-branch repo)          ; → string (current branch)
(git-checkout!      repo branch)   ; switch to branch
(git-branch-create! repo name)     ; create branch at HEAD
```

## Diff

```scheme
(git-diff        repo)   ; → string (unified diff, unstaged changes)
(git-diff-staged repo)   ; → string (unified diff, staged changes)
```

## Tags

```scheme
(git-tags        repo)                    ; → list of tag name strings
(git-tag-create! repo name message)       ; create annotated tag at HEAD
```

## Remotes

```scheme
(git-remotes repo)              ; → ((name . url) ...)
(git-fetch!  repo remote-name) ; fetch from remote
(git-push!   repo remote-name branch) ; push branch to remote
```

## Raw command

Use `git-command` (not yet exposed) for operations not covered above. As an alternative, use `(system "git ...")` for quick scripting.

## Examples

### Inspect a repository

```scheme
(import (curry git))
(import (scheme write))

(define repo (git-open "."))

(display "Branch: ") (display (git-head repo)) (newline)

(display "Status:\n")
(for-each
  (lambda (entry)
    (display "  ") (display (car entry))
    (display " ") (display (cdr entry)) (newline))
  (git-status repo))

(display "\nRecent commits:\n")
(for-each
  (lambda (commit)
    (display "  ")
    (display (substring (cdr (assq 'id commit)) 0 8))
    (display " ")
    (display (cdr (assq 'author commit)))
    (display ": ")
    (display (cdr (assq 'message commit)))
    (newline))
  (git-log repo 5))

(git-close! repo)
```

### Automate a commit

```scheme
(import (curry git))

(define repo (git-open "/path/to/repo"))

; Stage specific files
(git-add! repo "src/main.c")
(git-add! repo "docs/api.md")

; Commit
(define sha (git-commit! repo
  "feat: add D-dimensional gravity"
  "Alice"
  "alice@example.com"))

(display "Committed: ") (display sha) (newline)

(git-close! repo)
```

### Clone and analyse

```scheme
(import (curry git))

(define repo (git-clone "https://github.com/example/repo.git" "/tmp/repo"))

(display "Cloned. Commits: ")
(display (length (git-log repo)))
(newline)

(git-close! repo)
```

## Notes

- `git-clone` supports HTTPS and SSH URLs (SSH requires the system SSH agent or credentials to be configured).
- `git-commit!` uses the system clock for the commit timestamp.
- `git-push!` will fail without authentication configured in the system credential store or SSH agent.
- Large repositories may take time to clone or walk the log; consider running in an actor for non-blocking behaviour.
- libgit2 does not run git hooks; `pre-commit`, `post-commit`, etc. are bypassed.
