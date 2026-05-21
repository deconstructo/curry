#ifndef CURRY_SCC_H
#define CURRY_SCC_H

/*
 * scc — Scheme Compiled Cache
 *
 * A .scc file stores one Chunk per top-level expression, preserving the
 * one-at-a-time compilation semantics of the original interpreter loop.
 * The cache is valid when the source file's mtime and byte-size still match
 * the values recorded at write time, and the Curry version string is the same.
 *
 * Cache location — two-tier lookup:
 *   1. Source-adjacent:  <dir>/<name>.scc   (fast path for user-owned scripts)
 *   2. User cache:       ~/.cache/curry/<abs-path-mirrored>.scc
 *      Used when the source directory is not writable (system-installed scripts,
 *      read-only mounts, etc.).
 *
 * File layout:
 *   8 bytes    magic + format version  ("CURRYBC\x01")
 *   1 byte     CURRY_VERSION string length
 *   N bytes    CURRY_VERSION string (no NUL)
 *   8 bytes    source mtime  (int64_t, little-endian)
 *   8 bytes    source size   (int64_t, little-endian)
 *   4 bytes    number of chunks (uint32_t)
 *   ...        repeated Chunk records
 *   4 bytes    sentinel 0xCAFEBEEF
 */

#include <stdbool.h>
#include "chunk.h"

/* Write chunks to the best available cache location (source-adjacent, then
   ~/.cache/curry/ as fallback).  Silent on any error. */
void scc_write(const char *src_path, Chunk **chunks, int n_chunks);

/* Load cached chunks for src_path (tries source-adjacent, then ~/.cache/curry/).
   On success: populates *chunks_out (malloc'd array, caller frees) and *n_out,
   returns true.  Returns false if no valid cache found. */
bool scc_load(const char *src_path, Chunk ***chunks_out, int *n_out);

#endif /* CURRY_SCC_H */
