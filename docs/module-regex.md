# Module: (curry regex)

POSIX extended regular expressions using the system `<regex.h>` (glibc, musl, Darwin). No external library required.

## Import

```scheme
(import (curry regex))
```

## Compiling patterns

### `(regex-compile pattern [flags])` → *regex*

Compile a regex pattern string. Returns an opaque regex handle.

`flags` defaults to `REG_EXTENDED`. Pass one of the `REG_*` constants, or combine them with `bitwise-or`.

```scheme
(define rx (regex-compile "[A-Z][a-z]+"))
(define rx-i (regex-compile "hello" REG_ICASE))
```

### `(regex-free rx)`

Release the compiled pattern and free memory. Call this when the regex is no longer needed.

### `(regex? x)` → *boolean*

Returns `#t` if `x` is a regex handle.

## Matching

### `(regex-match rx string [eflags])` → `#f` | *list of (start . end)*

Test whether `string` matches `rx`. Returns `#f` on no match, or a list of `(start . end)` byte-offset pairs — one per capture group, with the full match first (group 0).

```scheme
(define rx (regex-compile "([a-z]+)@([a-z]+)\\.([a-z]+)"))
(regex-match rx "user@example.com")
; → ((0 . 15) (0 . 4) (5 . 12) (13 . 16))
```

### `(regex-match-string rx string [eflags])` → `#f` | *list of strings*

Like `regex-match` but returns matched substrings instead of byte offsets. Unmatched optional groups return `#f`.

```scheme
(regex-match-string rx "user@example.com")
; → ("user@example.com" "user" "example" "com")
```

## Replacement

### `(regex-replace rx string replacement [all?])` → *string*

Replace the first match (or all matches if `all?` is `#t`) with `replacement`. Use `\1`–`\9` in the replacement string to reference capture groups.

```scheme
(define rx (regex-compile "([a-z]+)@([a-z]+)"))
(regex-replace rx "foo@bar.com" "\\2@\\1")
; → "bar@foo.com"
(regex-replace (regex-compile "[aeiou]") "hello world" "*" #t)
; → "h*ll* w*rld"
```

## Splitting

### `(regex-split rx string)` → *list of strings*

Split `string` on occurrences of `rx`. Returns a list of the substrings between separators.

```scheme
(regex-split (regex-compile "[,;]+") "a,b;;c,d")
; → ("a" "b" "c" "d")
```

## Flag constants

| Constant | Meaning |
|----------|---------|
| `REG_EXTENDED` | POSIX extended syntax (default) |
| `REG_ICASE` | Case-insensitive matching |
| `REG_NOSUB` | Do not record match offsets |
| `REG_NEWLINE` | `^`/`$` match at embedded newlines |
| `REG_NOTBOL` | `^` does not match at start of string |
| `REG_NOTEOL` | `$` does not match at end of string |

## Resource management

Regex handles are plain pair/bytevector values with no GC finalizer. The underlying `regex_t` is heap-allocated with `malloc`. Call `(regex-free rx)` to release it; if you don't, the memory leaks when the Scheme handle is collected.

## Requirements

No external library required — uses the POSIX `<regex.h>` present on all supported platforms.
