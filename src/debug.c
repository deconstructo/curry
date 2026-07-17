/*
 * debug.c — interactive bytecode debugger.
 *
 * See debug.h for the model.  Everything here runs on the main thread
 * inside vm_run()'s dispatch loop, between two instructions: the current
 * op has fully committed its result, so vm->stack and vm->frames are
 * consistent and safe to inspect (the same invariant the minor-GC
 * safepoint relies on).
 *
 * Stop conditions:
 *   - function-name breakpoint: frame entry (ip == chunk->code)
 *   - file:line breakpoint:     first instruction of a new source line
 *   - step:    any new source line, or any frame entry
 *   - next:    same, but only at call depth <= the depth where 'next' ran
 *   - finish:  call depth drops below the depth where 'finish' ran
 *
 * "New source line" is tracked against the previously dispatched
 * instruction (last_chunk/last_line), not the previous stop, so multi-op
 * lines stop once and single-line loops still stop on re-entry.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#include "debug.h"
#include "vm.h"
#include "chunk.h"
#include "eval.h"
#include "env.h"
#include "symbol.h"
#include "port.h"
#include "reader.h"

volatile bool vm_debug_active = false;

typedef enum { DBG_RUN, DBG_STEP, DBG_NEXT, DBG_FINISH } DbgMode;

#define DBG_BREAKS_MAX 64

typedef struct {
    char *fn_name;   /* function-name breakpoint, or NULL      */
    char *file;      /* file part of a file:line breakpoint    */
    int   line;      /* line part; 0 when fn_name is set       */
    bool  used;
} Breakpoint;

static Breakpoint breaks[DBG_BREAKS_MAX];
static int        breaks_used = 0;

static DbgMode   mode = DBG_RUN;
static int       stop_depth = 0;         /* for DBG_NEXT / DBG_FINISH     */
static Chunk    *last_chunk = NULL;
static int       last_line  = -1;
static pthread_t main_thread;
static bool      initialized = false;
static char      last_cmd[256] = "";

void vm_debug_init(void) {
    main_thread = pthread_self();
    initialized = true;
}

static void update_active(void) {
    vm_debug_active = (breaks_used > 0) || (mode != DBG_RUN);
}

void vm_debug_request_step(void) {
    mode = DBG_STEP;
    /* force the very next dispatched instruction to count as a new line */
    last_chunk = NULL;
    last_line  = -1;
    update_active();
}

/* ── Breakpoint table ────────────────────────────────────────────────── */

int vm_debug_break_add(const char *spec) {
    if (!spec || !*spec) return -1;
    int idx = -1;
    for (int i = 0; i < DBG_BREAKS_MAX; i++)
        if (!breaks[i].used) { idx = i; break; }
    if (idx < 0) {
        fprintf(stderr, "debugger: breakpoint table full (max %d)\n",
                DBG_BREAKS_MAX);
        return -1;
    }
    Breakpoint *bp = &breaks[idx];
    /* "file:line" iff the last ':' is followed by digits only */
    const char *colon = strrchr(spec, ':');
    bool is_loc = false;
    if (colon && colon != spec && colon[1]) {
        is_loc = true;
        for (const char *p = colon + 1; *p; p++)
            if (!isdigit((unsigned char)*p)) { is_loc = false; break; }
    }
    if (is_loc) {
        bp->file = strndup(spec, (size_t)(colon - spec));
        bp->line = atoi(colon + 1);
        bp->fn_name = NULL;
    } else {
        bp->fn_name = strdup(spec);
        bp->file = NULL;
        bp->line = 0;
    }
    bp->used = true;
    breaks_used++;
    update_active();
    return idx;
}

bool vm_debug_break_remove(int idx) {
    if (idx < 0 || idx >= DBG_BREAKS_MAX || !breaks[idx].used) return false;
    free(breaks[idx].fn_name);
    free(breaks[idx].file);
    memset(&breaks[idx], 0, sizeof breaks[idx]);
    breaks_used--;
    update_active();
    return true;
}

void vm_debug_break_list(void) {
    if (breaks_used == 0) { puts("No breakpoints."); return; }
    for (int i = 0; i < DBG_BREAKS_MAX; i++) {
        if (!breaks[i].used) continue;
        if (breaks[i].fn_name)
            printf("  #%d  %s\n", i, breaks[i].fn_name);
        else
            printf("  #%d  %s:%d\n", i, breaks[i].file, breaks[i].line);
    }
}

/* ── Location helpers ────────────────────────────────────────────────── */

static const char *path_basename(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static bool file_matches(const char *source_name, const char *bp_file) {
    if (!source_name) return false;
    if (strcmp(source_name, bp_file) == 0) return true;
    return strcmp(path_basename(source_name), path_basename(bp_file)) == 0;
}

/* Line for a bytecode offset, matching vm_capture_backtrace's indexing. */
static int line_at(const Chunk *ch, int off) {
    if (!ch->lines || ch->code_len == 0) return 0;
    if (off < 0) off = 0;
    if (off >= ch->code_len) off = ch->code_len - 1;
    return ch->lines[off];
}

static void print_source_line(const char *file, int line) {
    if (!file || line <= 0) return;
    FILE *f = fopen(file, "r");
    if (!f) return;
    char buf[512];
    int n = 0;
    while (fgets(buf, sizeof buf, f)) {
        if (++n == line) {
            size_t len = strlen(buf);
            printf("  %4d | %s%s", line, buf,
                   (len && buf[len - 1] == '\n') ? "" : "\n");
            break;
        }
    }
    fclose(f);
}

static void print_frame_location(const CallFrame *fr, int line) {
    const Chunk *ch = fr->closure->chunk;
    printf("%s:%d: in %s\n",
           ch->source_name ? ch->source_name : "<unknown>",
           line,
           ch->name ? ch->name : "<anonymous>");
}

/* ── Inspection commands ─────────────────────────────────────────────── */

static void cmd_backtrace(const CallFrame *cur) {
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        const CallFrame *fr = &vm->frames[i];
        const Chunk *ch = fr->closure->chunk;
        int off = (int)(fr->ip - ch->code);
        /* outer frames' ip points past the call instruction */
        int line = line_at(ch, fr == cur ? off : off - 1);
        printf("  #%-2d %-24s %s:%d\n",
               vm->frame_count - 1 - i,
               ch->name ? ch->name : "<anonymous>",
               ch->source_name ? ch->source_name : "<unknown>",
               line);
    }
}

/* A local is shown iff its declaration range covers the current ip and
 * its slot is within the frame's live stack window. */
static void cmd_locals(const CallFrame *fr) {
    const Chunk *ch = fr->closure->chunk;
    int off = (int)(fr->ip - ch->code);
    int shown = 0;
    for (int i = 0; i < ch->local_debug_len; i++) {
        const LocalDebugEntry *e = &ch->local_debug[i];
        if (off < e->start_pc || off >= e->end_pc) continue;
        if (fr->slots + e->slot >= vm->sp) continue;
        printf("  %s = ", sym_cstr(e->name));
        scm_write(fr->slots[e->slot], PORT_STDOUT);
        putchar('\n');
        shown++;
    }
    if (shown == 0 && ch->local_debug_len == 0) {
        /* no debug table (e.g. pre-v3 .scc code): fall back to raw slots */
        int n = (int)(vm->sp - fr->slots);
        if (n > fr->slot_count) n = fr->slot_count;
        for (int i = 0; i < n; i++) {
            printf("  slot[%d] = ", i);
            scm_write(fr->slots[i], PORT_STDOUT);
            putchar('\n');
            shown++;
        }
    }
    /* captured variables (upvalues) */
    for (int i = 0; i < fr->closure->upval_count; i++) {
        Upvalue *uv = fr->closure->upvals[i];
        if (!uv) continue;
        if (ch->upval_names && ch->upval_names[i] != V_VOID)
            printf("  %s = ", sym_cstr(ch->upval_names[i]));
        else
            printf("  upval[%d] = ", i);
        scm_write(*uv->location, PORT_STDOUT);
        putchar('\n');
        shown++;
    }
    if (shown == 0) puts("  (no live locals)");
}

/* Look up a bare symbol among the frame's live locals; true on hit. */
static bool local_by_name(const CallFrame *fr, const char *name, val_t *out) {
    const Chunk *ch = fr->closure->chunk;
    int off = (int)(fr->ip - ch->code);
    /* scan backwards so the innermost shadowing declaration wins */
    for (int i = ch->local_debug_len - 1; i >= 0; i--) {
        const LocalDebugEntry *e = &ch->local_debug[i];
        if (off < e->start_pc || off >= e->end_pc) continue;
        if (fr->slots + e->slot >= vm->sp) continue;
        if (strcmp(sym_cstr(e->name), name) == 0) {
            *out = fr->slots[e->slot];
            return true;
        }
    }
    /* captured variables (upvalues) */
    if (ch->upval_names) {
        for (int i = 0; i < fr->closure->upval_count; i++) {
            Upvalue *uv = fr->closure->upvals[i];
            if (!uv || ch->upval_names[i] == V_VOID) continue;
            if (strcmp(sym_cstr(ch->upval_names[i]), name) == 0) {
                *out = *uv->location;
                return true;
            }
        }
    }
    return false;
}

/* Evaluate an expression typed at the dbg> prompt.  Stepping is disarmed
 * for the duration (the hook must not re-enter itself), and the VM stack
 * registers are restored if the expression raises, so an error in a
 * p-expression cannot corrupt the paused program. */
static void cmd_print(const CallFrame *fr, const char *text) {
    while (*text == ' ' || *text == '\t') text++;
    if (!*text) { puts("usage: p <expr>"); return; }

    /* bare symbol naming a live local: read the slot directly */
    bool bare_symbol = true;
    for (const char *p = text; *p; p++)
        if (isspace((unsigned char)*p) || *p == '(' || *p == ')' ||
            *p == '"' || *p == '\'') { bare_symbol = false; break; }
    if (bare_symbol) {
        val_t v;
        if (local_by_name(fr, text, &v)) {
            printf("  ");
            scm_write(v, PORT_STDOUT);
            putchar('\n');
            return;
        }
    }

    bool    saved_active = vm_debug_active;
    DbgMode saved_mode   = mode;
    vm_debug_active = false;
    mode            = DBG_RUN;

    val_t   *saved_sp    = vm->sp;
    int      saved_fc    = vm->frame_count;
    int      saved_hc    = vm->handler_count;
    Upvalue *saved_ouv   = vm->open_upvalues;

    ExnHandler h;
    SCM_PROTECT(h, {
        val_t expr = scm_read_cstr(text);
        if (vis_eof(expr)) {
            puts("  (unreadable expression)");
        } else {
            val_t result = vm_eval(expr, GLOBAL_ENV);
            printf("  ");
            scm_write(result, PORT_STDOUT);
            putchar('\n');
        }
    }, {
        vm->sp            = saved_sp;
        vm->frame_count   = saved_fc;
        vm->handler_count = saved_hc;
        vm->open_upvalues = saved_ouv;
        printf("  error: ");
        if (vis_error(h.exn)) scm_display(as_err(h.exn)->message, PORT_STDOUT);
        else                  scm_write(h.exn, PORT_STDOUT);
        putchar('\n');
    });

    mode            = saved_mode;
    vm_debug_active = saved_active;
}

/* ── The stop: command loop ──────────────────────────────────────────── */

static void print_help(void) {
    puts("Debugger commands:");
    puts("  s, step        stop at next line, entering calls");
    puts("  n, next        stop at next line, stepping over calls");
    puts("  f, finish      run until the current function returns");
    puts("  c, continue    run until the next breakpoint");
    puts("  bt             backtrace");
    puts("  locals         live local variables of the current frame");
    puts("  p <expr>       print a local by name, or evaluate globally");
    puts("  break <spec>   add breakpoint (function name or file:line)");
    puts("  unbreak <n>    remove breakpoint #n");
    puts("  breaks         list breakpoints");
    puts("  q, quit        abort evaluation, return to the REPL");
    puts("  (empty line repeats the previous command)");
}

static void debug_stop(CallFrame *frame, int line, const char *why) {
    if (why) printf("%s\n", why);
    print_frame_location(frame, line);
    print_source_line(frame->closure->chunk->source_name, line);

    char buf[256];
    for (;;) {
        printf("dbg> ");
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) {
            /* EOF on stdin: disarm and let the program run to completion */
            mode = DBG_RUN;
            update_active();
            return;
        }
        buf[strcspn(buf, "\n")] = '\0';
        if (!buf[0]) {
            if (!last_cmd[0]) continue;
            strcpy(buf, last_cmd);
        } else {
            strncpy(last_cmd, buf, sizeof last_cmd - 1);
            last_cmd[sizeof last_cmd - 1] = '\0';
        }

        if (!strcmp(buf, "s") || !strcmp(buf, "step")) {
            mode = DBG_STEP;
            update_active();
            return;
        }
        if (!strcmp(buf, "n") || !strcmp(buf, "next")) {
            mode = DBG_NEXT;
            stop_depth = vm->frame_count;
            update_active();
            return;
        }
        if (!strcmp(buf, "f") || !strcmp(buf, "finish")) {
            mode = DBG_FINISH;
            stop_depth = vm->frame_count;
            update_active();
            return;
        }
        if (!strcmp(buf, "c") || !strcmp(buf, "continue")) {
            mode = DBG_RUN;
            update_active();
            return;
        }
        if (!strcmp(buf, "bt")) { cmd_backtrace(frame); continue; }
        if (!strcmp(buf, "locals")) { cmd_locals(frame); continue; }
        if (!strncmp(buf, "p ", 2)) { cmd_print(frame, buf + 2); continue; }
        if (!strncmp(buf, "print ", 6)) { cmd_print(frame, buf + 6); continue; }
        if (!strncmp(buf, "break ", 6)) {
            int idx = vm_debug_break_add(buf + 6);
            if (idx >= 0) printf("Breakpoint #%d set.\n", idx);
            continue;
        }
        if (!strncmp(buf, "unbreak ", 8)) {
            if (!vm_debug_break_remove(atoi(buf + 8)))
                puts("No such breakpoint.");
            continue;
        }
        if (!strcmp(buf, "breaks")) { vm_debug_break_list(); continue; }
        if (!strcmp(buf, "h") || !strcmp(buf, "help")) { print_help(); continue; }
        if (!strcmp(buf, "q") || !strcmp(buf, "quit")) {
            mode = DBG_RUN;
            update_active();
            scm_raise(V_FALSE, "debugger: quit");
        }
        printf("Unknown command: %s  (h for help)\n", buf);
    }
}

/* ── The per-instruction hook ────────────────────────────────────────── */

void vm_debug_hook(CallFrame *frame) {
    if (!initialized || !pthread_equal(pthread_self(), main_thread)) return;

    Chunk *ch = frame->closure->chunk;
    int off  = (int)(frame->ip - ch->code);
    int line = line_at(ch, off);
    bool at_entry     = (off == 0);
    bool line_changed = (ch != last_chunk || line != last_line);
    /* a fresh boundary is a new line, or re-entry of a frame (covers
       single-line recursion and named-let loops, which never change line) */
    bool boundary = line_changed || at_entry;

    bool stop = false;
    char why[256];
    why[0] = '\0';

    switch (mode) {
    case DBG_STEP:   stop = boundary; break;
    case DBG_NEXT:   stop = boundary && vm->frame_count <= stop_depth; break;
    case DBG_FINISH: stop = vm->frame_count < stop_depth; break;
    case DBG_RUN:    break;
    }

    if (!stop && breaks_used > 0) {
        for (int i = 0; i < DBG_BREAKS_MAX && !stop; i++) {
            const Breakpoint *bp = &breaks[i];
            if (!bp->used) continue;
            if (bp->fn_name) {
                if (at_entry && ch->name && !strcmp(ch->name, bp->fn_name)) {
                    stop = true;
                    snprintf(why, sizeof why, "Breakpoint #%d: %s", i, bp->fn_name);
                }
            } else if (line == bp->line && line_changed &&
                       file_matches(ch->source_name, bp->file)) {
                stop = true;
                snprintf(why, sizeof why, "Breakpoint #%d: %s:%d",
                         i, bp->file, bp->line);
            }
        }
    }

    last_chunk = ch;
    last_line  = line;

    if (stop) debug_stop(frame, line, why[0] ? why : NULL);
}
