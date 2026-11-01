#ifndef CURRY_NESTING_DEPTH_H
#define CURRY_NESTING_DEPTH_H

#include <stdbool.h>
#include <stddef.h>

/* Track paren/bracket nesting across a whole multi-line buffer.

   Used for REPL continuation prompting (main.c's rl_read_expr) and the
   Jupyter kernel's is_complete_request. The state carries across lines, so
   a paren inside a string literal, a `|symbol|`, a `#| ... |#` block
   comment (nestable), a `#\(`-style character literal, or a `;` comment is
   never counted, even when the string or comment spans several lines.

   Usage: curry_nest_init(&n); then curry_nest_feed_line(&n, line) for each
   line (the implied newline is fed too), or curry_nest_feed(&n, buf, len)
   for raw text. Afterwards n.depth > 0 means more input is needed; so do
   n.in_str, n.in_bar and n.block > 0 (an unterminated string, symbol or
   block comment). */
typedef struct {
    int  depth;         /* open ( and [ minus closed ones */
    int  block;         /* #| ... |# nesting level */
    bool in_str;        /* inside "..." */
    bool in_bar;        /* inside |...| */
    bool esc;           /* previous char was a backslash inside str/bar */
    bool line_comment;  /* inside ; ... up to end of line */
    bool char_lit;      /* previous two chars were #\ ; next char is literal */
    char prev;          /* previous char outside strings, for #| |# #\ */
} CurryNest;

static inline void curry_nest_init(CurryNest *n) {
    n->depth = 0; n->block = 0;
    n->in_str = n->in_bar = n->esc = n->line_comment = n->char_lit = false;
    n->prev = 0;
}

static inline void curry_nest_feed(CurryNest *n, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (n->line_comment) {
            if (c == '\n') n->line_comment = false;
            n->prev = 0;
            continue;
        }
        if (n->block > 0) {
            if (n->prev == '|' && c == '#')      { n->block--; n->prev = 0; }
            else if (n->prev == '#' && c == '|') { n->block++; n->prev = 0; }
            else n->prev = c;
            continue;
        }
        if (n->esc)  { n->esc = false; continue; }
        if (n->in_str || n->in_bar) {
            if (c == '\\') n->esc = true;
            else if (n->in_str && c == '"') n->in_str = false;
            else if (n->in_bar && c == '|') n->in_bar = false;
            continue;
        }
        if (n->char_lit) { n->char_lit = false; n->prev = 0; continue; }
        if (n->prev == '#' && c == '|') { n->block = 1; n->prev = 0; continue; }
        if (n->prev == '#' && c == '\\') { n->char_lit = true; n->prev = 0; continue; }
        n->prev = c;
        if (c == ';')       n->line_comment = true;
        else if (c == '"')  n->in_str = true;
        else if (c == '|')  n->in_bar = true;
        else if (c == '(' || c == '[') n->depth++;
        else if (c == ')' || c == ']') n->depth--;
    }
}

static inline void curry_nest_feed_line(CurryNest *n, const char *line) {
    size_t len = 0;
    while (line[len]) len++;
    curry_nest_feed(n, line, len);
    curry_nest_feed(n, "\n", 1);
}

/* True while the buffer fed so far ends inside an unfinished form. */
static inline bool curry_nest_incomplete(const CurryNest *n) {
    return n->depth > 0 || n->in_str || n->in_bar || n->block > 0;
}

#endif
