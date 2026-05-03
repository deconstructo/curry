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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#define CURRY_VERSION "0.1.7"
#define BANNER \
    "Curry Scheme " CURRY_VERSION " (R7RS)\n" \
    "Type ,quit to exit, ,help for help.\n"

static void init_all(void) {
    gc_init();
    sym_init();
    num_init();
    port_init();
    env_init();
    eval_init();
    actors_init();
    modules_init();
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
                    puts("Commands: ,quit  ,help  ,gc  ,env");
                    continue;
                }
                if (!strcmp(name, "gc")) { gc_collect(); puts("GC complete."); continue; }
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
            val_t result = eval(expr, GLOBAL_ENV);
            current_handler = h.prev;
            if (print) print_result(result);
        } else {
            current_handler = h.prev;
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

/* ---- Usage ---- */

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options] [script.scm] [args...]\n"
        "  -e EXPR    Evaluate expression\n"
        "  -l FILE    Load file\n"
        "  -i         Force interactive REPL after loading scripts\n"
        "  -v         Print version\n"
        "  --         End of options\n",
        argv0);
}

/* ---- Entry point ---- */

int main(int argc, char **argv) {
    init_all();

    bool interactive = false;
    bool ran_something = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("Curry Scheme %s\n", CURRY_VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        }
        if (!strcmp(argv[i], "-i")) { interactive = true; continue; }
        if (!strcmp(argv[i], "--")) { i++; break; }

        if (!strcmp(argv[i], "-e")) {
            if (++i >= argc) { fputs("-e requires an argument\n", stderr); return 1; }
            val_t str_port = port_open_input_string(argv[i], (uint32_t)strlen(argv[i]));
            val_t last = V_VOID;
            for (;;) {
                val_t expr;
                ExnHandler h;
                h.prev = current_handler; current_handler = &h;
                if (setjmp(h.jmp) == 0) { expr = scm_read(str_port); current_handler = h.prev; }
                else { current_handler = h.prev; break; }
                if (vis_eof(expr)) break;
                h.prev = current_handler; current_handler = &h;
                if (setjmp(h.jmp) == 0) { last = eval(expr, GLOBAL_ENV); current_handler = h.prev; }
                else {
                    current_handler = h.prev;
                    fprintf(stderr, "Error: ");
                    if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDERR);
                    else scm_write(h.exn, PORT_STDERR);
                    fputs("\n", stderr);
                    return 1;
                }
            }
            print_result(last);
            ran_something = true;
            continue;
        }

        if (!strcmp(argv[i], "-l")) {
            if (++i >= argc) { fputs("-l requires an argument\n", stderr); return 1; }
            ExnHandler h;
            h.prev = current_handler; current_handler = &h;
            if (setjmp(h.jmp) == 0) { scm_load(argv[i], GLOBAL_ENV); current_handler = h.prev; }
            else { current_handler = h.prev;
                   fprintf(stderr, "Error loading %s: ", argv[i]);
                   scm_write(h.exn, PORT_STDERR); fputs("\n", stderr); return 1; }
            ran_something = true;
            continue;
        }

        /* Positional argument: script file */
        val_t cmd_line = V_NIL;
        for (int j = argc-1; j >= i; j--)
            cmd_line = scm_cons(sym_intern_cstr(argv[j]), cmd_line);
        env_define(GLOBAL_ENV, sym_intern_cstr("command-line-args"), cmd_line);

        ExnHandler h;
        h.prev = current_handler; current_handler = &h;
        if (setjmp(h.jmp) == 0) { scm_load(argv[i], GLOBAL_ENV); current_handler = h.prev; }
        else { current_handler = h.prev;
               fprintf(stderr, "Error: ");
               if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDERR);
               else scm_write(h.exn, PORT_STDERR);
               fputs("\n", stderr); return 1; }
        ran_something = true;
        break;
    }

    if (!ran_something || interactive) {
        repl();
    }
    return 0;
}
