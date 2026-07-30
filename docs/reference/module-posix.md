# Module: (curry posix)

*unreleased*

POSIX filesystem and process bindings — a pragmatic subset of [SRFI-170](https://srfi.schemers.org/srfi-170/) — plus [SRFI-112](https://srfi.schemers.org/srfi-112/) environment inquiry. Pure system libc (`sys/stat.h`, `dirent.h`, `unistd.h`, `pwd.h`, `grp.h`, `time.h`, `sys/utsname.h`); no external library dependency, macOS/Linux portable. Built by default (`-DBUILD_MODULE_POSIX=ON`).

Portable re-exports under each SRFI's own naming convention are available as `(srfi s170 posix)` and `(srfi s112 environment-inquiry)` — see [`module-srfi.md`](module-srfi.md).

## Import

```scheme
(import (curry posix))
```

## Scope

Implemented: file info (`stat`/`lstat`) and type predicates, directory create/list/remove, symlinks/hardlinks/rename, file mode/owner/times/truncation, process state (cwd, umask, pid, niceness, uid/gid), user/group database lookups, wall-clock and monotonic time, environment-variable mutation, a `terminal?` predicate, and (SRFI-112) implementation/OS/machine identity queries.

Deliberately **not** implemented in this first pass:

- **`posix-error?`/`posix-error-name`/`posix-error-message`** — every error here is raised as an ordinary catchable condition carrying `strerror()` text (see [Errors](#errors) below), but there's no separate error-type-introspection API yet; would need every call site to tag its `errno` consistently, a bigger design task than the rest of this module combined.
- **`open-file`/`fd->port`** — curry already has `open-input-file`/`open-output-file`; a full POSIX `O_*`-flag-based open is its own task and risks confusion with the existing R7RS port API.
- **`create-fifo`**, **`create-temp-file`/`call-with-temporary-filename`/`temp-file-prefix`**, **`file-space`** — lower-value, not yet done.
- **`make-directory-files-generator`** — SRFI-170's generator-based directory walker needs a generator protocol curry doesn't have; use `open-directory`/`read-directory`/`close-directory` instead (see below).
- Port-type and buffering-mode constants (`binary-input`, `buffer-line`, etc.) — irrelevant without `open-file`.

## Errors

Every procedure here raises an ordinary Scheme condition (catchable with `guard`) on failure, with a message of the form `posix: <procedure>: <strerror text>`:

```scheme
(guard (e (#t (display "failed: ") (display (condition-message e))))
  (file-info "/no/such/path"))
```

## File info

### `(file-info path [follow?])` → *file-info*

Stat `path`. `follow?` defaults to `#t` (follow symlinks, i.e. `stat(2)`); pass `#f` to inspect the link itself (`lstat(2)`).

### `(file-info? x)` → *boolean*

### Accessors

`file-info:device`, `file-info:inode`, `file-info:mode`, `file-info:nlinks`, `file-info:uid`, `file-info:gid`, `file-info:rdev`, `file-info:size`, `file-info:blksize`, `file-info:blocks` — all exact integers, straight from the underlying `struct stat` fields, packed into curry's 62-bit signed fixnum (no bignum promotion). Fine for any normal local filesystem; on NFS or certain overlay/network filesystems that synthesize inode numbers from a hash spanning the full 64-bit range, `file-info:inode`/`file-info:device` could in principle report an incorrect value if the real inode number doesn't fit 62 bits.

`file-info:atime`, `file-info:mtime`, `file-info:ctime` — exact integer seconds since the Unix epoch (whole-second granularity; the underlying nanosecond fields aren't exposed).

### Type predicates

`(file-info-directory? fi)`, `(file-info-regular? fi)`, `(file-info-symlink? fi)`, `(file-info-fifo? fi)`, `(file-info-socket? fi)`, `(file-info-device? fi)` (block device), `(file-info-char-device? fi)` — each takes a `file-info` object (not a path) and tests `st_mode & S_IFMT`.

```scheme
(define fi (file-info "/etc/passwd"))
(file-info-regular? fi)        ; => #t
(file-info:size fi)            ; => exact integer, bytes
(file-info-symlink? (file-info "/etc/localtime" #f))  ; #f follows-link=no
```

## Directories

### `(create-directory path [mode])`

`mkdir(2)`. `mode` defaults to `#o777` (modified by the process umask as usual).

### `(delete-directory path)`

`rmdir(2)` — the directory must be empty.

### `(directory-files path [dotfiles?])` → *list of strings*

Filenames in `path`, excluding `.`/`..`. Dotfiles (names starting with `.`) are excluded unless `dotfiles?` is true.

### `(open-directory path)` → *directory-stream*
### `(read-directory stream)` → *string* | *eof-object*
### `(close-directory stream)`

Lower-level streaming alternative to `directory-files` for large directories — `.`/`..` are still skipped automatically.

```scheme
(define ds (open-directory "."))
(let loop ()
  (let ((name (read-directory ds)))
    (unless (eof-object? name)
      (display name) (newline)
      (loop))))
(close-directory ds)
```

## Links, rename, paths

- `(rename-file old new)` — `rename(2)`.
- `(create-hard-link old new)` — `link(2)`.
- `(create-symlink target linkpath)` — `symlink(2)`; `target` is stored verbatim (relative or absolute), not resolved.
- `(read-symlink path)` → *string* — `readlink(2)`, the link's stored target text.
- `(real-path path)` → *string* — `realpath(2)`, fully resolved absolute path (symlinks followed, `.`/`..` collapsed).
- `(truncate-file path length)` — `truncate(2)`.
- `(set-file-mode path mode)` — `chmod(2)`; `mode` is a plain integer, e.g. `#o644`.
- `(set-file-owner path uid [gid])` — `chown(2)`; `gid` defaults to unchanged.
- `(set-file-times path [atime [mtime]])` — `utimensat(2)`; each of `atime`/`mtime` is exact seconds since epoch (or a flonum), `#f`, or omitted to mean "set to now".

## Process state

- `(umask)` → current umask, read without changing it. POSIX has no syscall to peek the umask without also setting it, so this does `umask(0)` immediately followed by `umask(old)` to restore it — the process-wide umask is transiently `0` between those two calls. In a curry program with multiple actor threads, a file created by a *different* actor in that narrow window gets no umask applied. This is inherent to the umask API (no portable peek-only syscall exists) — avoid calling `(umask)` from a hot path in a program that's also concurrently creating files from other actors if the exact permission bits matter.
- `(set-umask! mask)` — set the umask, discarding the old value.
- `(current-directory)` → *string* — `getcwd(2)`.
- `(set-current-directory! path)` — `chdir(2)`.
- `(pid)` → *exact integer* — `getpid(2)`.
- `(nice)` → current process niceness (via `getpriority`, read-only).
- `(nice increment)` → adjust niceness by `increment` and return the new value (via `getpriority`+`setpriority`, avoiding the `errno`-ambiguity in POSIX `nice(2)`'s return value).
- `(user-uid)`, `(user-gid)`, `(user-effective-uid)`, `(user-effective-gid)` → exact integers.
- `(user-supplementary-gids)` → *list of exact integers* — `getgroups(2)`.

## User/group database

### `(user-info uid-or-name)` → *user-info*

Uses the reentrant `getpwuid_r`/`getpwnam_r` with a 4096-byte buffer; a pathologically large GECOS/shell/home-dir field that overflows it raises an error rather than retrying with a larger buffer (fine for any real-world `/etc/passwd` entry).

### `(user-info? x)` → *boolean*
### Accessors: `user-info:name`, `user-info:uid`, `user-info:gid`, `user-info:home-dir`, `user-info:shell`, `user-info:full-name`

### `(group-info gid-or-name)` → *group-info*
### `(group-info? x)` → *boolean*
### Accessors: `group-info:name`, `group-info:gid`

```scheme
(define me (user-info (user-uid)))
(user-info:name me)      ; => "alice"
(user-info:home-dir me)  ; => "/home/alice"
(user-info (user-info:name me))  ; look up by name instead of uid
```

## Time

- `(posix-time)` → *flonum* — seconds since the Unix epoch, `CLOCK_REALTIME`, sub-second precision.
- `(monotonic-time)` → *flonum* — seconds from an arbitrary, monotonically-increasing reference point (`CLOCK_MONOTONIC`); suitable for measuring elapsed time, not for wall-clock display.

## Environment

- `(set-environment-variable! name value)` — `setenv(3)`, overwriting any existing value.
- `(delete-environment-variable! name)` — `unsetenv(3)`.

(Reading environment variables uses curry's existing core R7RS procedures: `get-environment-variable`, `get-environment-variables`.)

## Terminal

### `(terminal? [fd-or-port])` → *boolean*

`isatty(3)`. Takes a file descriptor (exact integer), a curry port, or nothing (defaults to fd 0, stdin).

```scheme
(terminal? 1)                    ; is stdout a tty?
(terminal? (current-output-port))
```

## Environment inquiry (SRFI-112)

Six zero-argument procedures, each returning a string or `#f` if the implementation can't provide it. A portable re-export under the SRFI's own naming convention is available as `(srfi s112 environment-inquiry)`.

- `(implementation-name)` → `"curry"`.
- `(implementation-version)` → curry's own version string (e.g. `"1.11.1"`).
- `(cpu-architecture)` → `uname(2)`'s `machine` field (e.g. `"arm64"`, `"x86_64"`).
- `(os-name)` → `uname(2)`'s `sysname` field (e.g. `"Darwin"`, `"Linux"`).
- `(os-version)` → `uname(2)`'s `release` field.
- `(machine-name)` → `gethostname(2)`.

```scheme
(import (curry posix))
(list (implementation-name) (implementation-version) (os-name))
; => ("curry" "1.11.1" "Darwin")
```
