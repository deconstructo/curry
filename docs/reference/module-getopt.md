# Module: (curry getopt)

General-purpose command-line option parser. Supports short flags (`-x`), long flags (`--foo`), value options (`-n 40`, `--steps=40`), clustered short flags (`-xyz`), positional arguments, and `--` end-of-options.

## Import

```scheme
(import (curry getopt))
```

## Defining options

### `(option name short long has-arg? default description)` → *spec*

Constructs an option specification.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | symbol | Key used to retrieve the parsed value |
| `short` | char or `#f` | Short flag character, e.g. `#\n`; `#f` if none |
| `long` | string or `#f` | Long flag name without `--`, e.g. `"steps"`; `#f` if none |
| `has-arg?` | boolean | `#t` if the option requires a value argument |
| `default` | any | Value used when the option is absent; `#f` for flags |
| `description` | string | Description shown in usage output |

```scheme
(define specs
  (list
    (option 'steps #\n "steps" #t "40" "number of generations")
    (option 'verbose #\v "verbose" #f #f "enable verbose output")
    (option 'help   #\h "help"   #f #f  "show help")))
```

## Parsing

### `(getopt args specs)` → *result*

Parse a list of argument strings against the option specs. Returns an opaque result object.

`args` is typically `(cdr command-line-args)` to skip the script name.

```scheme
(define result (getopt (cdr command-line-args) specs))
```

**Short flags** may be clustered: `-vx` sets both `v` and `x`. A value option consumes the rest of the cluster as its value (`-n40`) or the next argument (`-n 40`).

**Long flags** accept `--foo` (flag) or `--foo=bar` / `--foo bar` (value).

**Positional arguments** — any argument not starting with `-`, or any argument after `--`, is collected as a positional.

**`--`** signals end of options; all subsequent arguments are treated as positionals.

## Accessors

### `(opt-get result name)` → *value* or `#f`

Return the parsed value for `name` (a symbol matching the spec's `name` field). Returns the default if the option was not supplied, or `#t` for flags that were set.

```scheme
(opt-get result 'steps)    ; => "40" (default) or user-supplied string
(opt-get result 'verbose)  ; => #t if -v was passed, #f otherwise
```

Note: value options always return strings; convert with `string->number` as needed.

### `(opt-rest result)` → *list of strings*

Return the list of positional arguments (in order).

```scheme
(opt-rest result)   ; => ("file1.txt" "file2.txt")
```

### `(opt-errors result)` → *list of strings*

Return a list of parse error messages. Empty when parsing succeeded.

```scheme
(opt-errors result)  ; => () on success
                     ; => ("unknown option: --foo" ...) on error
```

### `(opt-ok? result)` → *boolean*

Returns `#t` if there were no parse errors.

```scheme
(unless (opt-ok? result)
  (for-each (lambda (e) (display e) (newline)) (opt-errors result))
  (exit 1))
```

## Usage string

### `(opt-usage program specs)` → *string*

Return a formatted usage string suitable for display. Lists all options with their flags, description, and default value (if any).

```scheme
(display (opt-usage "myprog" specs))
```

Output example:

```
Usage: myprog [options] [args...]
Options:
  -n, --steps=VAL         number of generations [40]
  -v, --verbose           enable verbose output
  -h, --help              show help
```

## Error handling

Unknown options, missing required values, and flags given an unexpected value all produce entries in `opt-errors`. The parser is non-aborting — it records errors and continues, so all errors are collected in one pass.

## Complete example

```scheme
#!/usr/bin/env curry
(import (curry getopt))

(define specs
  (list
    (option 'output #\o "output" #t "a.out" "output file")
    (option 'verbose #\v "verbose" #f #f    "verbose mode")
    (option 'help   #\h "help"   #f #f     "show help")))

(define result (getopt (cdr command-line-args) specs))

(when (opt-get result 'help)
  (display (opt-usage "mytool" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e) (newline)) (opt-errors result))
  (display (opt-usage "mytool" specs))
  (exit 1))

(define outfile  (opt-get result 'output))
(define verbose? (opt-get result 'verbose))
(define files    (opt-rest result))

(when verbose?
  (display (string-append "writing to: " outfile "\n")))
```

Running this script:

```sh
mytool -v --output=result.txt foo.txt bar.txt
mytool -vo result.txt foo.txt bar.txt   ; cluster: -v and -o result.txt
mytool -- --not-a-flag.txt              ; positional starting with --
```
