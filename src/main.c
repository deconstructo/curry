#include "gc.h"
#include "gc_gen.h"
#ifdef BUILD_LLVM
#  include "llvm/curry_llvm.h"
#endif
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "eval.h"
#include "reader.h"
#include "builtins.h"
#include "actors.h"
#include "stm.h"
#include "channel.h"
#include "condition.h"
#ifdef BUILD_FFI
#  include "curry_ffi.h"
#endif
#include "sx_rules.h"
#include "sx_algebra.h"
#include "modules.h"
#include "object.h"
#include "profiling.h"
#include "vm.h"
#include "debug.h"
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

#ifdef BUILD_LLVM
#  define LLVM_TAG " (LLVM)"
#else
#  define LLVM_TAG ""
#endif

#ifdef BUILD_FFI
#  define FFI_TAG " (FFI)"
#else
#  define FFI_TAG ""
#endif

#define BANNER \
    "𒋗𒈬 𒌝 𒄿𒈾 𒋗  |  šulmu — šiprī ina qātīka\n" \
    "Greetings — my service is in your hands\n\n"\
    "Curry Scheme " CURRY_VERSION " (R7RS)" LLVM_TAG FFI_TAG "\n" \
    "Type ,quit to exit, ,help for help.\n\n"


static void init_all(void) {
    gc_init();
    sym_init();
    num_init();
    port_init();
    env_init();
    eval_init();
    sx_rules_init();
    sx_algebra_init();
    actors_init();
    stm_init();
    channel_init();
    condition_init();
#ifdef BUILD_FFI
    ffi_init();
#endif
    modules_init();
    profiling_init(GLOBAL_ENV);
    vm_init();
    vm_debug_init();
#ifdef BUILD_LLVM
    curry_llvm_init();
#endif
}

/* ---- Error reporting ---- */

/* Print "Error: <message>" plus, when available, a backtrace of
   (name file line) frames captured at raise time. Shared by every
   top-level catch site (script, REPL, -e, -c, .scc loading). */
static void print_scheme_error(val_t exn) {
    if (vis_condition(exn)) {
        /* (curry conditions)'s CL-style condition, e.g. from condition-error
           (tts-error, srfi-215's log conditions, etc.) -- distinct from the
           R7RS ErrorObj case below (different struct, different fields).
           Without this case, an uncaught condition fell through to the
           generic scm_write_shared fallback, which has no print case for
           T_CONDITION either, and printed as a bare "#<object 46>" (46 =
           T_CONDITION's tag) with no indication of what actually failed. */
        Condition *c = as_condition(exn);
        if (vis_symbol(c->type_sym)) fprintf(stderr, "Error [%s]: ", sym_cstr(c->type_sym));
        else fprintf(stderr, "Error: ");
        if (vis_string(c->message)) scm_display_shared(c->message, PORT_STDERR);
        else scm_write_shared(exn, PORT_STDERR);
        fputs("\n", stderr);
        return;
    }
    val_t code = vis_error(exn) ? as_err(exn)->code : V_FALSE;
    if (vis_symbol(code)) fprintf(stderr, "Error [%s]: ", sym_cstr(code));
    else fprintf(stderr, "Error: ");
    if (vis_error(exn)) scm_display_shared(as_err(exn)->message, PORT_STDERR);
    else scm_write_shared(exn, PORT_STDERR);
    fputs("\n", stderr);

    val_t bt = vis_error(exn) ? as_err(exn)->backtrace : V_NIL;
    for (val_t f = bt; vis_pair(f); f = vcdr(f)) {
        val_t frame = vcar(f);
        val_t name  = vcar(frame);
        val_t file  = vcadr(frame);
        val_t line  = vcaddr(frame);
        fputs("  at ", stderr);
        if (vis_string(name)) scm_display(name, PORT_STDERR);
        else fputs("?", stderr);
        if (vis_string(file)) {
            fputs(" (", stderr);
            scm_display(file, PORT_STDERR);
            if (vis_fixnum(line)) fprintf(stderr, ":%ld", (long)vunfix(line));
            fputs(")", stderr);
        } else if (vis_fixnum(line)) {
            fprintf(stderr, " (line %ld)", (long)vunfix(line));
        }
        fputs("\n", stderr);
    }
}

/* ---- REPL ---- */

static void print_result(val_t v) {
    if (vis_void(v)) return;
    if (vis_values(v)) {
        Values *mv = as_vals(v);
        for (uint32_t i = 0; i < mv->count; i++) {
            scm_write_shared(mv->vals[i], PORT_STDOUT);
            if (i + 1 < mv->count) scm_newline(PORT_STDOUT);
        }
        scm_newline(PORT_STDOUT);
        return;
    }
    scm_write_shared(v, PORT_STDOUT);
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
    compiler_set_source_name("<repl>");
    for (;;) {
        val_t expr;
        ExnHandler h;
        h.prev = current_handler; current_handler = &h;
        uint64_t _read_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
        if (setjmp(h.jmp) == 0) {
            expr = scm_read(port);
            current_handler = h.prev;
            if (curry_timings_enabled)
                curry_timing_read_ns += profiling_now_ns() - _read_t0;
        }
        else {
            current_handler = h.prev;
            fprintf(stderr, "Read error: ");
            scm_write_shared(h.exn, PORT_STDERR);
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
                    exit(0);   /* runs the atexit(curry_timings_report) hook */
                }
                if (!strcmp(name, "help")) {
                    puts("Commands: ,quit  ,help  ,gc  ,env  ,profile  ,vm");
                    puts("Debugger: ,break <fn|file:line>  ,unbreak <n>  ,breaks  ,debug <expr>");
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
                           (unsigned long)GC_get_gc_no(),
                           GC_get_gc_no() == 1 ? "" : "s");
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
                    EnvFrame *f = as_env(GLOBAL_ENV);
                    for (uint32_t i = 0; i < f->size; i++) {
                        scm_display(f->syms[i], PORT_STDOUT);
                        scm_newline(PORT_STDOUT);
                    }
                    continue;
                }
                if (!strcmp(name, "break")) {
                    val_t arg = scm_read(port);
                    if (vis_symbol(arg)) {
                        int idx = vm_debug_break_add(sym_cstr(arg));
                        if (idx >= 0) printf("Breakpoint #%d set.\n", idx);
                    } else if (vis_string(arg)) {
                        int idx = vm_debug_break_add(str_data(as_str(arg)));
                        if (idx >= 0) printf("Breakpoint #%d set.\n", idx);
                    } else {
                        fprintf(stderr, "usage: ,break <function|file:line>\n");
                    }
                    continue;
                }
                if (!strcmp(name, "unbreak")) {
                    val_t arg = scm_read(port);
                    if (!vis_fixnum(arg) || !vm_debug_break_remove((int)vunfix(arg)))
                        fprintf(stderr, "usage: ,unbreak <index>  (see ,breaks)\n");
                    continue;
                }
                if (!strcmp(name, "breaks")) { vm_debug_break_list(); continue; }
                if (!strcmp(name, "debug")) {
                    expr = scm_read(port);
                    if (vis_eof(expr)) break;
                    vm_debug_request_step();
                    goto eval_expr;   /* run it under single-step */
                }
                fprintf(stderr, "Unknown REPL command: ,%s\n", name);
                continue;
            }
        }

    eval_expr:
        h.prev = current_handler;
        h.saved_jit_depth = jit_depth_save();
        current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            uint64_t _t0 = curry_timings_enabled ? profiling_now_ns() : 0;
            val_t cl     = compiler_compile(expr);
            if (curry_timings_enabled) {
                uint64_t _t1 = profiling_now_ns();
                curry_timing_compile_ns += _t1 - _t0;
                _t0 = _t1;
            }
            val_t result = vm_run(as_bcclosure(cl), 0);
            if (curry_timings_enabled)
                curry_timing_execute_ns += profiling_now_ns() - _t0;
            current_handler = h.prev;
            if (print) print_result(result);
        } else {
            current_handler = h.prev;
            jit_depth_restore(h.saved_jit_depth);
            vm_reset();
            print_scheme_error(h.exn);
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

/* Returns true for forms that must be RUN (not just compiled) during -c
   compilation because they affect the macro/import environment for
   subsequent forms in the same -c session — e.g. so a later top-level use
   of a macro defined earlier in the same file sees it.

   Note this is about running the compiled BYTECODE; it doesn't gate
   whether -c's "without executing" guarantee holds for macro-related
   forms in general. It never fully has: compiling ANY macro use site
   already evaluates that macro's transformer procedure (via apply()) at
   compile time, -c included — that's inherent to how syntax-rules/macro
   expansion works in any Scheme, not something -c can opt out of. compiler.c's
   compile_define_syntax/compile_let_syntax extend this the same way for
   macro DEFINITION (not just use): a define-syntax/let-syntax/letrec-syntax
   form's transformer-expr is evaluated during compilation regardless of -c,
   so a pathological transformer-expr with an observable side effect (I/O, a
   counter, etc.) runs once per compile even under -c. This is a pre-existing
   characteristic of compiling this language, not a gap this filter is meant
   to close — Curry source is fully trusted, comparable to any Scheme eval. */
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

/* Timed scm_read wrapper — for call sites (like the cache-miss script-load
 * loop below) where the read happens inline in a while-condition and can't
 * be bracketed with explicit before/after timestamps without restructuring
 * the loop. A read that itself raises (longjmp) skips the accumulator add,
 * same as any other error path — harmless, just under-counts that one call. */
static val_t timed_scm_read(val_t port) {
    uint64_t t0 = curry_timings_enabled ? profiling_now_ns() : 0;
    val_t v = scm_read(port);
    if (curry_timings_enabled) curry_timing_read_ns += profiling_now_ns() - t0;
    return v;
}

/* Pending .scc write, flushed via atexit — see write_pending_scc_atexit.
 * g_pending_scc_src armed (non-NULL) means "everything compiled into
 * g_pending_scc_chunks[0..g_pending_scc_n) so far is safe to cache". */
static const char *g_pending_scc_src     = NULL;
static Chunk      **g_pending_scc_chunks = NULL;
static int          g_pending_scc_n      = 0;

/* Same root cause as the --timings/atexit fix above, for the same class of
 * program: the positional-script-file cache-miss loop (below) only calls
 * scc_write() once, AFTER its while loop returns — but vm_run() on a form
 * that never returns to its caller (e.g. (curry qt6)'s run-event-loop,
 * which calls exit(3) itself once the Qt event loop returns, deep inside
 * vm_run()) means that line is never reached, so a GUI script's .scc cache
 * was never written on ANY run — always a full MISS, defeating the entire
 * point of the transparent cache for exactly the class of program (slow
 * GUI startup) that benefits from it most.
 *
 * Fixed the same way: register this via atexit() once, and have the loop
 * arm it incrementally as each chunk is appended (which happens BEFORE
 * that chunk's vm_run() — see the loop below — so by the time a
 * never-returning vm_run() runs, the chunks compiled so far, including the
 * current one, are already recorded here and safe to cache exactly as they
 * are). The loop clears g_pending_scc_src after its own explicit
 * end-of-loop scc_write() to avoid writing the file twice on a normal
 * exit; the error handler for this whole block also clears it, so a
 * genuine compile/runtime error never leaves behind a partial cache that a
 * later unchanged-content run would wrongly treat as a complete, valid
 * HIT (silently truncating execution to whatever compiled before the
 * error). */
static void write_pending_scc_atexit(void) {
    if (g_pending_scc_src)
        scc_write(g_pending_scc_src, g_pending_scc_chunks, g_pending_scc_n);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options] [script.scm] [args...]\n"
        "  -e EXPR           Evaluate expression\n"
        "  -l FILE           Load file\n"
        "  -c FILE           Compile FILE to .scc bytecode without executing\n"
        "  -o OUT            Output path for -c (default: FILE with .scc extension)\n"
        "  -x                Make -c output executable (shebang + chmod +x)\n"
        "  -i                Force interactive REPL after loading scripts\n"
        "  -b SPEC           Set a debugger breakpoint before running (function\n"
        "                    name or file:line; repeatable)\n"
        "  -v                Print version\n"
        "  --gc BACKEND      GC backend: boehm (default) or generational (experimental)\n"
        "  --gc-max-heap N   Limit GC heap (suffixes K/M/G, e.g. 256M; 0 = unlimited)\n"
        "  --gc-nursery-size N  Per-thread nursery size (default 512K; requires --gc generational)\n"
        "                    Note: generational backend is experimental. Actor-heavy workloads\n"
        "                    may accumulate nursery garbage between main-thread GC cycles.\n"
        "  --timings         Print a read/expand/compile/execute pipeline timing\n"
        "                    report to stderr on exit\n"
        "  --clear-cache     Delete any existing .scc cache for the script being\n"
        "                    run (both cache-location tiers) before compiling,\n"
        "                    forcing a fresh compile instead of a stale cache hit\n",
        argv0);
}

/* Parse a size string like "256M", "1G", "512K", or a plain integer (bytes). */
static val_t prim_command_line(int ac, val_t *av, void *ud) {
    (void)ac; (void)av;
    return (val_t)(uintptr_t)ud;
}

static size_t parse_size(const char *s) {
    char *end;
    unsigned long long v = strtoull(s, &end, 10);
    if      (*end == 'K' || *end == 'k') v *= 1024ULL;
    else if (*end == 'M' || *end == 'm') v *= 1024ULL * 1024;
    else if (*end == 'G' || *end == 'g') v *= 1024ULL * 1024 * 1024;
    return (size_t)v;
}

/* ---- Entry point ---- */

int main(int argc, char **argv) {
    /* Register once, unconditionally: curry_timings_report() itself checks
     * curry_timings_enabled at the time it actually runs, so it's harmless
     * to register before that flag is even parsed below. atexit — rather
     * than explicit calls at the end of main() and the REPL's ,quit — is
     * what makes this correct for paths that skip main()'s normal return
     * entirely: e.g. (curry qt6)'s run-event-loop calls C's exit(3) itself
     * once the Qt event loop returns (qt6.cpp), deep inside vm_run(), long
     * before control would ever get back to the end of main(). exit(3)
     * (unlike _exit(2)) always runs atexit handlers, so this is the one
     * place a hook can catch every exit path uniformly. */
    atexit(curry_timings_report);
    atexit(write_pending_scc_atexit);   /* see write_pending_scc_atexit above */

    /* Pre-scan for --gc and --gc-nursery-size before any GC initialisation. */
    size_t nursery_size = 0;   /* 0 = use gc_gen default */
    bool use_gen_gc = false;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--gc") == 0) {
            if (strcmp(argv[i+1], "generational") == 0) {
                use_gen_gc = true;
            } else if (strcmp(argv[i+1], "boehm") != 0) {
                fprintf(stderr,
                    "curry: unknown GC backend '%s' (use 'boehm' or 'generational')\n",
                    argv[i+1]);
                return 1;
            }
        } else if (strcmp(argv[i], "--gc-nursery-size") == 0) {
            nursery_size = parse_size(argv[i+1]);
        }
    }
    if (use_gen_gc) {
        gc_gen_init(nursery_size);
        gc_ops = &gc_gen_ops;
    }
    init_all();

    bool interactive = false;
    bool ran_something = false;
    const char *compile_file = NULL;
    const char *compile_out  = NULL;
    bool compile_executable  = false;
    bool clear_cache_flag    = false;

    static const struct option long_opts[] = {
        { "version",      no_argument,       NULL, 'v' },
        { "help",         no_argument,       NULL, 'h' },
        { "gc-max-heap",  required_argument, NULL, 'G' },
        { "gc",              required_argument, NULL, 0   },  /* handled pre-scan */
        { "gc-nursery-size", required_argument, NULL, 0   },  /* handled pre-scan */
        { "timings",         no_argument,       NULL, 0   },
        { "clear-cache",     no_argument,       NULL, 0   },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    int long_idx;
    /* '+' prefix: stop at first non-option so script args aren't consumed */
    while ((opt = getopt_long(argc, argv, "+e:l:c:o:b:ixvhG:", long_opts, &long_idx)) != -1) {
        switch (opt) {
        case 0:
            /* long option with no short form */
            if (!strcmp(long_opts[long_idx].name, "timings"))
                curry_timings_enabled = true;
            else if (!strcmp(long_opts[long_idx].name, "clear-cache"))
                clear_cache_flag = true;
            break;   /* --gc / --gc-nursery-size already handled in the pre-scan */
        case 'v':
            printf("Curry Scheme %s (R7RS)" LLVM_TAG FFI_TAG "\n", CURRY_VERSION);
            return 0;
        case 'h':
            usage(argv[0]); return 0;
        case 'G':
            gc_set_max_heap(parse_size(optarg)); break;
        case 'i':
            interactive = true; break;
        case 'b':
            if (vm_debug_break_add(optarg) < 0) {
                fprintf(stderr, "Error: bad breakpoint spec: %s\n", optarg);
                return 1;
            }
            break;
        case 'x':
            compile_executable = true; break;
        case 'o':
            compile_out = optarg; break;
        case 'c':
            compile_file = optarg; break;
        case 'e': {
            compiler_set_source_name("<expr>");
            val_t str_port = port_open_input_string(optarg, (uint32_t)strlen(optarg));
            val_t last = V_VOID;
            for (;;) {
                val_t expr;
                ExnHandler h;
                gc_inhibit_minor();
                h.prev = current_handler; current_handler = &h;
                uint64_t _read_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
                if (setjmp(h.jmp) == 0) {
                    expr = scm_read(str_port);
                    current_handler = h.prev;
                    if (curry_timings_enabled)
                        curry_timing_read_ns += profiling_now_ns() - _read_t0;
                }
                else { gc_resume_minor(); current_handler = h.prev; break; }
                if (vis_eof(expr)) { gc_resume_minor(); break; }
                h.prev = current_handler;
                h.saved_jit_depth = jit_depth_save();
                current_handler = &h;
                if (setjmp(h.jmp) == 0) {
                    uint64_t _t0 = curry_timings_enabled ? profiling_now_ns() : 0;
                    val_t cl = compiler_compile(expr);
                    gc_resume_minor();
                    if (curry_timings_enabled) {
                        uint64_t _t1 = profiling_now_ns();
                        curry_timing_compile_ns += _t1 - _t0;
                        _t0 = _t1;
                    }
                    last = vm_run(as_bcclosure(cl), 0);
                    if (curry_timings_enabled)
                        curry_timing_execute_ns += profiling_now_ns() - _t0;
                    current_handler = h.prev;
                } else {
                    current_handler = h.prev;
                    jit_depth_restore(h.saved_jit_depth);
                    vm_reset();
                    print_scheme_error(h.exn);
                    return 1;
                }
            }
            print_result(last);
            ran_something = true;
            break;
        }
        case 'l': {
            ExnHandler h;
            h.prev = current_handler;
            h.saved_jit_depth = jit_depth_save();
            current_handler = &h;
            if (setjmp(h.jmp) == 0) { scm_load(optarg, GLOBAL_ENV); current_handler = h.prev; }
            else {
                current_handler = h.prev;
                jit_depth_restore(h.saved_jit_depth);
                fprintf(stderr, "Error loading %s: ", optarg);
                scm_write_shared(h.exn, PORT_STDERR); fputs("\n", stderr); return 1;
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
        compiler_set_source_name(compile_file);
        ExnHandler h;
        h.prev = current_handler;
        h.saved_jit_depth = jit_depth_save();
        current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            val_t v;
            while (!vis_eof((v = timed_scm_read(port)))) {
                uint64_t _t0 = curry_timings_enabled ? profiling_now_ns() : 0;
                val_t cl = compiler_compile(v);
                if (curry_timings_enabled) {
                    uint64_t _t1 = profiling_now_ns();
                    curry_timing_compile_ns += _t1 - _t0;
                    _t0 = _t1;
                }
                BcClosure *bc = as_bcclosure(cl);
                if (n_chunks == cap) {
                    cap *= 2;
                    chunks = GC_REALLOC(chunks, (size_t)cap * sizeof(Chunk *));
                }
                chunks[n_chunks++] = bc->chunk;
                if (affects_compile_env(v)) {
                    vm_run(bc, 0);
                    if (curry_timings_enabled)
                        curry_timing_execute_ns += profiling_now_ns() - _t0;
                }
            }
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            jit_depth_restore(h.saved_jit_depth);
            vm_reset();
            print_scheme_error(h.exn);
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
            cmd_line = scm_cons(scm_symbol_to_string(sym_intern_cstr(argv[j])), cmd_line);
        env_define(GLOBAL_ENV, sym_intern_cstr("command-line-args"), cmd_line);
        Primitive *cmd_prim = CURRY_NEW_PINNED(Primitive);
        cmd_prim->hdr.type = T_PRIMITIVE; cmd_prim->hdr.flags = 0;
        cmd_prim->name = "command-line"; cmd_prim->min_args = 0; cmd_prim->max_args = 0;
        cmd_prim->fn = prim_command_line; cmd_prim->ud = (void *)(uintptr_t)cmd_line;
        env_define(GLOBAL_ENV, sym_intern_cstr("command-line"), vptr(cmd_prim));

        ExnHandler h;
        h.prev = current_handler;
        h.saved_jit_depth = jit_depth_save();
        current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            /* So a top-level script's own (load "relative/path.scm")
             * resolves against the script's own directory, not the
             * process's cwd -- see scm_load's header comment in
             * runtime.c. A failure anywhere in this branch falls through
             * to the setjmp else-branch below, which prints the error
             * and exits the process (return 1) rather than continuing,
             * so this doesn't need SCM_PROTECT-style release-then-
             * reraise on the exceptional path the way scm_load/
             * load_scheme_module do -- there's no "later, unrelated
             * load" left in this process for a leaked mark to corrupt. */
            int load_dir_script_mark = load_dir_mark();
            load_push_dir(argv[i]);
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
            /* --clear-cache: discard any existing cache for this script
             * before deciding HIT or MISS, so a stale .scc (e.g. left
             * behind by a curry build with a different bytecode format
             * mid-development -- see scc_clear's own comment) is never
             * consulted, and the run below always recompiles fresh and
             * rewrites it. No-op when the positional argument is itself a
             * .scc file (is_scc): there is no separate cache to clear in
             * that case, the file *is* the thing being run. */
            if (clear_cache_flag && !is_scc)
                scc_clear(argv[i]);
            if (is_scc) {
                /* Direct .scc run — no source file, skip mtime check.
                   .scc caching doesn't persist source_name (see scc.c), so
                   stamp it post-load for backtraces; argv[] outlives the run. */
                if (!scc_load_direct(argv[i], &chunks, &n_chunks)) {
                    fprintf(stderr, "Error: cannot load bytecode file: %s\n", argv[i]);
                    current_handler = h.prev;
                    return 1;
                }
                for (int k = 0; k < n_chunks; k++)
                    chunk_set_source_name_recursive(chunks[k], argv[i]);
                uint64_t _exec_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
                for (int k = 0; k < n_chunks; k++)
                    vm_run(vm_make_closure(chunks[k], 0), 0);
                if (curry_timings_enabled)
                    curry_timing_execute_ns += profiling_now_ns() - _exec_t0;
            } else if (scc_load(argv[i], &chunks, &n_chunks)) {
                /* Cache hit: run each chunk in order */
                curry_timings_cache_checked = true;
                curry_timings_cache_hit     = true;
                for (int k = 0; k < n_chunks; k++)
                    chunk_set_source_name_recursive(chunks[k], argv[i]);
                uint64_t _exec_t0 = curry_timings_enabled ? profiling_now_ns() : 0;
                for (int k = 0; k < n_chunks; k++)
                    vm_run(vm_make_closure(chunks[k], 0), 0);
                if (curry_timings_enabled)
                    curry_timing_execute_ns += profiling_now_ns() - _exec_t0;
            } else {
                /* Cache miss: compile one form at a time (preserves macro semantics),
                   collect chunks, write cache; each form is run as compiled */
                curry_timings_cache_checked = true;
                curry_timings_cache_hit     = false;
                compiler_set_source_name(argv[i]);
                val_t port = port_open_file(argv[i], PORT_INPUT);
                if (vis_false(port)) {
                    fprintf(stderr, "Error: cannot open file: %s\n", argv[i]);
                    return 1;
                }
                int cap = 64;
                chunks = GC_MALLOC((size_t)cap * sizeof(Chunk *));
                val_t v;
                gc_inhibit_minor();
                while (!vis_eof((v = timed_scm_read(port)))) {
                    uint64_t _t0 = curry_timings_enabled ? profiling_now_ns() : 0;
                    val_t cl = compiler_compile(v);
                    gc_resume_minor();   /* safe region: vm_run */
                    if (curry_timings_enabled) {
                        uint64_t _t1 = profiling_now_ns();
                        curry_timing_compile_ns += _t1 - _t0;
                        _t0 = _t1;
                    }
                    BcClosure *bc = as_bcclosure(cl);
                    if (n_chunks == cap) {
                        cap *= 2;
                        chunks = GC_REALLOC(chunks, (size_t)cap * sizeof(Chunk *));
                    }
                    chunks[n_chunks++] = bc->chunk;
                    /* Arm the atexit fallback with everything compiled so
                     * far, INCLUDING this chunk, before running it — see
                     * write_pending_scc_atexit: vm_run may never return. */
                    g_pending_scc_src    = argv[i];
                    g_pending_scc_chunks = chunks;
                    g_pending_scc_n      = n_chunks;
                    vm_run(bc, 0);
                    if (curry_timings_enabled)
                        curry_timing_execute_ns += profiling_now_ns() - _t0;
                    gc_inhibit_minor(); /* back to unsafe for next read/compile */
                }
                gc_resume_minor();   /* EOF exit: leave inhibit balanced */
                port_close(port);
                scc_write(argv[i], chunks, n_chunks);
                g_pending_scc_src = NULL;   /* already written — don't also write at exit */
            }
            load_dir_release(load_dir_script_mark);
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            jit_depth_restore(h.saved_jit_depth);
            vm_reset();
            print_scheme_error(h.exn);
            /* A genuine compile/runtime error must not leave behind a
             * partial cache: a later unchanged-content run would treat it
             * as a complete, valid HIT and silently truncate execution to
             * whatever compiled before the error. */
            g_pending_scc_src = NULL;
            return 1;
        }
        ran_something = true;
    }

    if (!ran_something || interactive) {
        repl();
    }
    return 0;   /* runs the atexit(curry_timings_report) hook */
}
