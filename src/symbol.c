#include "symbol.h"
#include "object.h"
#include "gc.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* Open-addressing hash table mapping name -> Symbol* */
typedef struct {
    Symbol **buckets;
    uint32_t cap;
    uint32_t size;
} SymTable;

static SymTable table;

static uint32_t hash_str(const char *s, uint32_t len) {
    /* FNV-1a */
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static void table_insert_raw(SymTable *t, Symbol *sym) {
    uint32_t mask = t->cap - 1;
    uint32_t idx  = sym->hash & mask;
    while (t->buckets[idx]) idx = (idx + 1) & mask;
    t->buckets[idx] = sym;
    t->size++;
}

static void table_grow(void) {
    uint32_t new_cap = table.cap ? table.cap * 2 : 64;
    /* Use GC_MALLOC_UNCOLLECTABLE for the bucket array so it's a GC root */
    Symbol **new_buckets = GC_MALLOC_UNCOLLECTABLE(new_cap * sizeof(Symbol *));
    memset(new_buckets, 0, new_cap * sizeof(Symbol *));
    SymTable new_t = { new_buckets, new_cap, 0 };
    for (uint32_t i = 0; i < table.cap; i++) {
        if (table.buckets[i]) table_insert_raw(&new_t, table.buckets[i]);
    }
    if (table.buckets) GC_FREE(table.buckets);
    table = new_t;
}

val_t sym_intern(const char *name, uint32_t len) {
    if (!table.cap || table.size * 2 >= table.cap) table_grow();

    uint32_t h    = hash_str(name, len);
    uint32_t mask = table.cap - 1;
    uint32_t idx  = h & mask;

    while (table.buckets[idx]) {
        Symbol *s = table.buckets[idx];
        if (s->hash == h && s->len == len && memcmp(s->data, name, len) == 0)
            return vptr(s);
        idx = (idx + 1) & mask;
    }

    /* Not found: allocate new symbol (atomic: no interior GC pointers) */
    Symbol *sym = (Symbol *)gc_alloc_atomic(sizeof(Symbol) + len + 1);
    sym->hdr.type  = T_SYMBOL;
    sym->hdr.flags = 0;
    sym->len  = len;
    sym->hash = h;
    memcpy(sym->data, name, len);
    sym->data[len] = '\0';

    table.buckets[idx] = sym;
    table.size++;
    return vptr(sym);
}

val_t sym_intern_cstr(const char *name) {
    return sym_intern(name, (uint32_t)strlen(name));
}

const char *sym_cstr(val_t v) { return as_sym(v)->data; }
uint32_t    sym_len(val_t v)  { return as_sym(v)->len;  }

/* ---- Pre-interned symbols ---- */
#define SYM(var, str) val_t var;
#include "symbol_list.h"
#undef SYM

void sym_init(void) {
    table_grow(); /* initial allocation */
#define SYM(var, str) var = sym_intern_cstr(str);
#include "symbol_list.h"
#undef SYM
}
