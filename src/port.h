#ifndef CURRY_PORT_H
#define CURRY_PORT_H

/*
 * I/O ports for Curry Scheme (R7RS §6.13).
 *
 * A port is either a file port (wrapping a C FILE*) or a string port
 * (backed by an in-memory buffer).  Ports may be textual or binary.
 *
 * Character encoding: UTF-8 throughout.
 * Line endings: normalized to #\newline on input.
 */

#include "value.h"
#include <stdint.h>
#include <stdio.h>

/* ---- Constructors ---- */
val_t port_open_file(const char *path, int flags);  /* PORT_INPUT | PORT_OUTPUT | PORT_BINARY */
val_t port_open_input_string(const char *str, uint32_t len);
val_t port_open_output_string(void);
val_t port_open_input_bytevector(const uint8_t *data, uint32_t len);
val_t port_open_output_bytevector(void);
val_t port_wrap_file(FILE *fp, int flags);

/* Wrap an existing file descriptor (e.g. a socket) as a port: fdopen()s it
 * with a mode string chosen from flags (mirroring port_open_file's mode
 * logic) and registers a GC finalizer to close it, same as a file opened
 * via port_open_file. Returns V_FALSE if fdopen fails. */
val_t port_wrap_fd(int fd, int flags);

/* Wrap an already-open FILE* (e.g. a funopen/fopencookie custom stream
 * backed by something other than a plain fd, such as a TLS session) as a
 * port, taking ownership: registers the same auto-close-on-GC finalizer
 * port_open_file/port_wrap_fd do. Unlike port_wrap_file (used only for
 * stdin/stdout/stderr, which must never be auto-closed), this is for
 * "this FILE* is the port's exclusive property." */
val_t port_wrap_file_owned(FILE *fp, int flags);

/* ---- Standard ports ---- */
extern val_t PORT_STDIN, PORT_STDOUT, PORT_STDERR;
void port_init(void);

/* ---- Predicates ---- */
bool port_is_input(val_t p);
bool port_is_output(val_t p);
bool port_is_binary(val_t p);
bool port_is_open(val_t p);

/* ---- Character I/O ---- */
int   port_read_char(val_t p);            /* returns Unicode codepoint or -1 on EOF */
int   port_peek_char(val_t p);
bool  port_char_ready(val_t p);
void  port_unread_char(val_t p, int ch);
void  port_write_char(val_t p, int ch);   /* writes UTF-8 encoding */
int   port_line(val_t p);                 /* 1-based line of the next unread char */

/* ---- String / line I/O ---- */
val_t port_read_line(val_t p);            /* returns string or eof */
void  port_write_string(val_t p, const char *s, uint32_t len);

/* ---- Byte I/O ---- */
int  port_read_byte(val_t p);   /* 0..255 or -1 */
int  port_peek_byte(val_t p);
void port_write_byte(val_t p, uint8_t b);

/* ---- Control ---- */
void  port_close(val_t p);
val_t port_get_output_string(val_t p);  /* for string output ports */
val_t port_get_output_bytevector(val_t p);

/* ---- Scheme-level display / write ---- */
void  scm_display(val_t v, val_t port);       /* (display obj port) */
void  scm_write(val_t v, val_t port);         /* (write obj port) - readable output */
void  scm_write_shared(val_t v, val_t port);  /* (write-shared obj port) */
void  scm_display_shared(val_t v, val_t port); /* cycle-safe display, used by plain (display) */
void  scm_newline(val_t port);

#endif /* CURRY_PORT_H */
