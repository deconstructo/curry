#ifndef CURRY_NESTING_DEPTH_H
#define CURRY_NESTING_DEPTH_H

#include <stdbool.h>

/* Track paren/bracket nesting depth change across one line of text.
   Handles strings and ; line comments; good enough for interactive input
   (REPL continuation prompting, Jupyter is_complete_request). Callers
   accumulate this across lines to decide whether an expression is
   balanced yet -- see main.c's rl_read_expr for the reference use. */
static inline int curry_line_depth(const char *s) {
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

#endif
