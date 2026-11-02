#ifndef CURRY_EDITOR_ASSIST_H
#define CURRY_EDITOR_ASSIST_H

#include <stdbool.h>
#include <stddef.h>

/* Shared by modules/lsp/lsp.c and the Jupyter kernel; see editor_assist.c. */

/* Deepest paren/bracket nesting the reader is asked to handle. */
#define ASSIST_MAX_READER_DEPTH 1000
#define ASSIST_MAX_LOCAL_NAMES  256

/* True if `text` nests deeper than ASSIST_MAX_READER_DEPTH, so it must not be
 * handed to scm_read (the reader recurses once per level). */
bool assist_nesting_too_deep(const char *text);

typedef struct {
    char *names[ASSIST_MAX_LOCAL_NAMES];
    int   n;
} AssistNames;

/* Names bound by define/lambda/let-family/do forms in `text`, found by
 * reading it. Stops at the first syntax error, keeping what it has. */
void assist_collect_local_names(const char *text, AssistNames *out);
void assist_names_free(AssistNames *out);

/* LSP CompletionItemKind values. */
enum { ASSIST_FUNCTION = 3, ASSIST_VARIABLE = 6, ASSIST_KEYWORD = 14 };

typedef void (*AssistCandidateFn)(const char *label, int kind,
                                  const char *detail, void *ud);

/* Calls fn for every builtin, special form, Akkadian synonym, and name bound
 * in `text`. Unfiltered and may repeat a label; callers filter and dedupe. */
void assist_candidates(const char *text, AssistCandidateFn fn, void *ud);

/* Calls fn for every name bound in the live global environment. */
void assist_global_names(AssistCandidateFn fn, void *ud);

/* The identifier at 0-based (line, character) in `text`, or NULL. Malloc'd. */
char *assist_token_at(const char *text, long line, long character);

/* The identifier containing byte `offset` (or ending exactly at it), as the
 * byte range [*start, *end). False when there is none. */
bool assist_token_range(const char *text, size_t offset, size_t *start, size_t *end);

/* Markdown hover text for a builtin, special form or Akkadian synonym.
 * False if `tok` is none of those. */
bool assist_describe(const char *tok, char *out, size_t cap);

#endif
