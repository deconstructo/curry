#include "gc.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "eval.h"
#include "reader.h"
#include "builtins.h"
#include "actors.h"
#include "modules.h"
#include "object.h"
#include "profiling.h"
#include "vm.h"
#include "compiler.h"
#include "scc.h"
#include "version.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

#ifdef HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#define BANNER \
    "𒋗𒈬 𒌝 𒄿𒈾 𒋗  |  šulmu — šiprī ina qātīka\n" \
    "Greetings — my service is in your hands\n\n"\
    "Curry Scheme " CURRY_VERSION " (R7RS)\n" \
    "Type ,quit to exit, ,help for help.\n\n" 


static void init_all(void) {
    gc_init();
    sym_init();
    num_init();
    port_init();
    env_init();
    eval_init();
    actors_init();
    modules_init();
    profiling_init(GLOBAL_ENV);
    vm_init();
}

/* ---- REPL ---- */

static void print_result(val_t v) {
    if (vis_void(v)) return;
    if (vis_values(v)) {
        Values *mv = as_vals(v);
        for (uint32_t i = 0; i < mv->count; i++) {
            scm_write(mv->vals[i], PORT_STDOUT);
            if (i + 1 < mv->count) scm_newline(PORT_STDOUT);
        }
        scm_newline(PORT_STDOUT);
        return;
    }
    scm_write(v, PORT_STDOUT);
    scm_newline(PORT_STDOUT);
}

#ifdef HAVE_READLINE

#define HISTORY_FILE "/.curry_history"
#define HISTORY_MAX  500

static void rl_load_history(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s" HISTORY_FILE, home);
    read_history(path);
}

static void rl_save_history(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s" HISTORY_FILE, home);
    write_history(path);
    history_truncate_file(path, HISTORY_MAX);
}

/* Track nesting depth change across one line of text.
   Handles strings and ; line comments; good enough for interactive input. */
static int line_depth(const char *s) {
    int d = 0;
    bool in_str = false, esc = false;
    for (; *s; s++) {
        if (esc)    { esc = false; continue; }
        if (in_str) {
            if (*s == '\\') esc = true;
            else if (*s == '"') in_str = false;
            continue;
        }
        if (*s == ';') break;           /* line comment */
        if (*s == '"') { in_str = true; continue; }
        if (*s == '(' || *s == '[') d++;
        else if (*s == ')' || *s == ']') d--;
    }
    return d;
}

/* Read a complete Scheme expression via readline, accumulating lines until
   parentheses balance.  Returns a malloc'd NUL-terminated string the caller
   must free, or NULL on EOF. */
static char *rl_read_expr(void) {
    char *line = readline("> ");
    if (!line) return NULL;

    int depth = line_depth(line);

    if (depth <= 0) {
        /* Single-line expression (atom, quoted form, or balanced parens) */
        if (*line) add_history(line);
        char *result = strdup(line);
        free(line);
        return result;
    }

    /* Multi-line: accumulate lines until depth reaches 0 */
    size_t used = strlen(line);
    size_t cap  = used + 256;
    char  *buf  = malloc(cap);
    memcpy(buf, line, used);
    buf[used++] = '\n';
    if (*line) add_history(line);
    free(line);

    while (depth > 0) {
        char *more = readline("... ");
        if (!more) break;               /* EOF mid-expression */
        if (*more) add_history(more);
        size_t mlen = strlen(more);
        if (used + mlen + 2 > cap) {
            cap = (used + mlen + 2) * 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + used, more, mlen);
        used += mlen;
        buf[used++] = '\n';
        depth += line_depth(more);
        free(more);
    }
    buf[used] = '\0';
    return buf;
}

#endif /* HAVE_READLINE */

static void eval_port_exprs(val_t port, bool print) {
    for (;;) {
        val_t expr;
        ExnHandler h;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) { expr = scm_read(port); current_handler = h.prev; }
        else {
            current_handler = h.prev;
            fprintf(stderr, "Read error: ");
            scm_write(h.exn, PORT_STDERR);
            fprintf(stderr, "\n");
            continue;
        }
        if (vis_eof(expr)) break;

        /* REPL commands: ,name is read as (unquote name) by the Scheme reader */
        {
            val_t cmd = V_FALSE;
            if (vis_pair(expr) &&
                as_pair(expr)->car == S_UNQUOTE &&
                vis_pair(as_pair(expr)->cdr) &&
                vis_symbol(as_pair(as_pair(expr)->cdr)->car))
                cmd = as_pair(as_pair(expr)->cdr)->car;

            if (vis_symbol(cmd)) {
                const char *name = sym_cstr(cmd);
                if (!strcmp(name, "quit") || !strcmp(name, "exit")) {
#ifdef HAVE_READLINE
                    rl_save_history();
#endif
                    exit(0);
                }
                if (!strcmp(name, "help")) {
                    puts("Commands: ,quit  ,help  ,gc  ,env  ,profile  ,vm");
                    continue;
                }
                if (!strcmp(name, "gc")) { gc_collect(); puts("GC complete."); continue; }
                if (!strcmp(name, "vm")) {
                    size_t heap  = GC_get_heap_size();
                    size_t free_ = GC_get_free_bytes();
                    size_t used  = heap > free_ ? heap - free_ : 0;
                    size_t life  = GC_get_total_bytes();
                    int    sp    = (int)(vm->sp - vm->stack);
                    printf("  heap:    %.1f MB used / %.1f MB total\n",
                           used / 1048576.0, heap / 1048576.0);
                    printf("  alloc:   %.1f MB lifetime\n", life / 1048576.0);
                    printf("  gc:      %lu collection%s\n",
                           (unsigned long)GC_gc_no,
                           GC_gc_no == 1 ? "" : "s");
                    printf("  stack:   %d / %d slots\n",  sp, VM_STACK_MAX);
                    printf("  frames:  %d / %d\n", vm->frame_count, VM_FRAMES_MAX);
                    continue;
                }
                if (!strcmp(name, "profile")) {
                    val_t report = profiling_report();
                    if (vis_nil(report)) {
                        puts("No profiling data. Set (set! **eval-profiler** 1) to enable.");
                    } else {
                        printf("%-40s %10s %12s\n", "function", "calls", "ns-total");
                        printf("%-40s %10s %12s\n", "--------", "-----", "--------");
                        for (val_t p = report; vis_pair(p); p = vcdr(p)) {
                            val_t entry = vcar(p);
                            const char *nm = sym_cstr(vcar(entry));
                            val_t inner = vcdr(entry);
                            long long calls = (long long)vunfix(vcar(inner));
                            long long ns    = (long long)vunfix(vcdr(inner));
                            printf("%-40s %10lld %12lld\n", nm, calls, ns);
                        }
                    }
                    continue;
                }
                if (!strcmp(name, "env")) {
                    EnvFrame *f = as_env(GLOBAL_ENV)->frame;
                    for (uint32_t i = 0; i < f->size; i++) {
                        scm_display(f->syms[i], PORT_STDOUT);
                        scm_newline(PORT_STDOUT);
                    }
                    continue;
                }
                fprintf(stderr, "Unknown REPL command: ,%s\n", name);
                continue;
            }
        }

        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            val_t cl     = compiler_compile(expr);
            val_t result = vm_run(as_bcclosure(cl), 0);
            current_handler = h.prev;
            if (print) print_result(result);
        } else {
            current_handler = h.prev;
            vm_reset();
            fprintf(stderr, "Error: ");
            if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDERR);
            else scm_write(h.exn, PORT_STDERR);
            fprintf(stderr, "\n");
        }
    }
}

static void repl(void) {
    fprintf(stdout, "%s", BANNER);
    fflush(stdout);

#ifdef HAVE_READLINE
    rl_load_history();
    while (1) {
        char *input = rl_read_expr();
        if (!input) { fprintf(stdout, "\n"); break; }  /* EOF */
        uint32_t len = (uint32_t)strlen(input);
        val_t port = port_open_input_string(input, len);
        free(input);
        eval_port_exprs(port, true);
    }
    rl_save_history();
#else
    if (isatty(fileno(stdin))) { fputs("> ", stdout); fflush(stdout); }
    eval_port_exprs(PORT_STDIN, true);
    if (isatty(fileno(stdin))) fprintf(stdout, "\n");
#endif
}

/* ---- Compile-time execution filter ---- */

/* Returns true for forms that must be run during -c compilation because
   they affect the macro/import environment for subsequent forms. */
static bool affects_compile_env(val_t form) {
    if (!vis_pair(form)) return false;
    val_t head = vcar(form);
    if (head == S_IMPORT || head == S_DEFINE_SYNTAX) return true;
    if (head == S_BEGIN) {
        val_t rest = vcdr(form);
        while (vis_pair(rest)) {
            if (affects_compile_env(vcar(rest))) return true;
            rest = vcdr(rest);
        }
    }
    return false;
}

/* ---- Usage ---- */

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options] [script.scm] [args...]\n"
        "  -e EXPR    Evaluate expression\n"
        "  -l FILE    Load file\n"
        "  -c FILE    Compile FILE to .scc bytecode without executing\n"
        "  -o OUT     Output path for -c (default: FILE with .scc extension)\n"
        "  -x         Make -c output executable (shebang + chmod +x)\n"
        "  -i         Force interactive REPL after loading scripts\n"
        "  -v         Print version\n",
        argv0);
}

/* ---- Entry point ---- */

int main(int argc, char **argv) {
    init_all();

    bool interactive = false;
    bool ran_something = false;
    const char *compile_file = NULL;
    const char *compile_out  = NULL;
    bool compile_executable  = false;

    static const struct option long_opts[] = {
        { "version", no_argument, NULL, 'v' },
        { "help",    no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    /* '+' prefix: stop at first non-option so script args aren't consumed */
    while ((opt = getopt_long(argc, argv, "+e:l:c:o:ixvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'v':
            printf("Curry Scheme %s\n", CURRY_VERSION);
            return 0;
        case 'h':
            usage(argv[0]); return 0;
        case 'i':
            interactive = true; break;
        case 'x':
            compile_executable = true; break;
        case 'o':
            compile_out = optarg; break;
        case 'c':
            compile_file = optarg; break;
        case 'e': {
            val_t str_port = port_open_input_string(optarg, (uint32_t)strlen(optarg));
            val_t last = V_VOID;
            for (;;) {
                val_t expr;
                ExnHandler h;
                h.prev = current_handler; current_handler = &h;
                if (setjmp(h.jmp) == 0) { expr = scm_read(str_port); current_handler = h.prev; }
                else { current_handler = h.prev; break; }
                if (vis_eof(expr)) break;
                h.prev = current_handler; current_handler = &h;
                if (setjmp(h.jmp) == 0) {
                    val_t cl = compiler_compile(expr);
                    last = vm_run(as_bcclosure(cl), 0);
                    current_handler = h.prev;
                } else {
                    current_handler = h.prev;
                    vm_reset();
                    fprintf(stderr, "Error: ");
                    if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDERR);
                    else scm_write(h.exn, PORT_STDERR);
                    fputs("\n", stderr);
                    return 1;
                }
            }
            print_result(last);
            ran_something = true;
            break;
        }
        case 'l': {
            ExnHandler h;
            h.prev = current_handler; current_handler = &h;
            if (setjmp(h.jmp) == 0) { scm_load(optarg, GLOBAL_ENV); current_handler = h.prev; }
            else {
                current_handler = h.prev;
                fprintf(stderr, "Error loading %s: ", optarg);
                scm_write(h.exn, PORT_STDERR); fputs("\n", stderr); return 1;
            }
            ran_something = true;
            break;
        }
        default:
            usage(argv[0]); return 1;
        }
    }

    /* Run pending compilation (-c, with optional -o and -x) */
    if (compile_file) {
        val_t port = port_open_file(compile_file, PORT_INPUT);
        if (vis_false(port)) {
            fprintf(stderr, "Error: cannot open file: %s\n", compile_file);
            return 1;
        }
        int cap = 64;
        Chunk **chunks = GC_MALLOC((size_t)cap * sizeof(Chunk *));
        int n_chunks = 0;
        ExnHandler h;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            val_t v;
            while (!vis_eof((v = scm_read(port)))) {
                val_t cl = compiler_compile(v);
                BcClosure *bc = as_bcclosure(cl);
                if (n_chunks == cap) {
                    cap *= 2;
                    chunks = GC_REALLOC(chunks, (size_t)cap * sizeof(Chunk *));
                }
                chunks[n_chunks++] = bc->chunk;
                if (affects_compile_env(v))
                    vm_run(bc, 0);
            }
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            vm_reset();
            fprintf(stderr, "Error: ");
            if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDERR);
            else scm_write(h.exn, PORT_STDERR);
            fputs("\n", stderr);
            return 1;
        }
        port_close(port);
        if (compile_out) {
            scc_write_to(compile_out, compile_file, chunks, n_chunks, compile_executable);
            fprintf(stderr, "Compiled %s -> %s (%d chunk%s)\n",
                    compile_file, compile_out, n_chunks, n_chunks == 1 ? "" : "s");
        } else {
            scc_write(compile_file, chunks, n_chunks);
            fprintf(stderr, "Compiled %s (%d chunk%s)\n",
                    compile_file, n_chunks, n_chunks == 1 ? "" : "s");
        }
        ran_something = true;
    }

    /* Positional argument: script file (first non-option) */
    if (optind < argc) {
        int i = optind;
        val_t cmd_line = V_NIL;
        for (int j = argc-1; j >= i; j--)
            cmd_line = scm_cons(sym_intern_cstr(argv[j]), cmd_line);
        env_define(GLOBAL_ENV, sym_intern_cstr("command-line-args"), cmd_line);

        ExnHandler h;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            Chunk **chunks = NULL;
            int n_chunks = 0;
            size_t arglen = strlen(argv[i]);
            bool is_scc = arglen > 4 && strcmp(argv[i] + arglen - 4, ".scc") == 0;
            /* Also detect bytecode by magic bytes (supports -o with no .scc ext).
               Only probe regular files — pipes/FDs (e.g. process substitution)
               cannot be re-read after probing. */
            if (!is_scc) {
                struct stat _st;
                if (stat(argv[i], &_st) == 0 && S_ISREG(_st.st_mode)) {
                    FILE *probe = fopen(argv[i], "rb");
                    if (probe) {
                        char buf[24]; int c;
                        int n = 0;
                        int c1 = fgetc(probe), c2 = fgetc(probe);
                        if (c1 == '#' && c2 == '!')
                            while ((c = fgetc(probe)) != EOF && c != '\n') {}
                        else { buf[n++] = (char)c1; if (c2 != EOF) buf[n++] = (char)c2; }
                        n += (int)fread(buf + n, 1, (size_t)(7 - n), probe);
                        if (n >= 7 && memcmp(buf, "CURRYBC", 7) == 0) is_scc = true;
                        fclose(probe);
                    }
                }
            }
            if (is_scc) {
                /* Direct .scc run — no source file, skip mtime check */
                if (!scc_load_direct(argv[i], &chunks, &n_chunks)) {
                    fprintf(stderr, "Error: cannot load bytecode file: %s\n", argv[i]);
                    current_handler = h.prev;
                    return 1;
                }
                for (int k = 0; k < n_chunks; k++)
                    vm_run(vm_make_closure(chunks[k], 0), 0);
            } else if (scc_load(argv[i], &chunks, &n_chunks)) {
                /* Cache hit: run each chunk in order */
                for (int k = 0; k < n_chunks; k++)
                    vm_run(vm_make_closure(chunks[k], 0), 0);
            } else {
                /* Cache miss: compile one form at a time (preserves macro semantics),
                   collect chunks, write cache; each form is run as compiled */
                val_t port = port_open_file(argv[i], PORT_INPUT);
                if (vis_false(port)) {
                    fprintf(stderr, "Error: cannot open file: %s\n", argv[i]);
                    return 1;
                }
                int cap = 64;
                chunks = GC_MALLOC((size_t)cap * sizeof(Chunk *));
                val_t v;
                while (!vis_eof((v = scm_read(port)))) {
                    val_t cl = compiler_compile(v);
                    BcClosure *bc = as_bcclosure(cl);
                    if (n_chunks == cap) {
                        cap *= 2;
                        chunks = GC_REALLOC(chunks, (size_t)cap * sizeof(Chunk *));
                    }
                    chunks[n_chunks++] = bc->chunk;
                    vm_run(bc, 0);
                }
                port_close(port);
                scc_write(argv[i], chunks, n_chunks);
            }
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            vm_reset();
            fprintf(stderr, "Error: ");
            if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDERR);
            else scm_write(h.exn, PORT_STDERR);
            fputs("\n", stderr);
            return 1;
        }
        ran_something = true;
    }

    if (!ran_something || interactive) {
        repl();
    }
    return 0;
}
