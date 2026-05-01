#ifndef CURRY_VALUE_H
#define CURRY_VALUE_H

/*
 * Core value representation for Curry Scheme.
 *
 * A val_t is a 64-bit tagged integer encoding either an immediate value or a
 * pointer to a GC-managed heap object.  The low 2 bits are the tag:
 *
 *   00  heap pointer  (object is 8-byte aligned; low 3 bits are zero)
 *   01  fixnum        (62-bit signed integer; value = (intptr_t)v >> 2)
 *   10  character     (Unicode codepoint; value = v >> 8)
 *   11  immediate     (#f, #t, '(), void, eof, ...)
 *
 * R7RS truth: everything except #f is truthy.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uintptr_t val_t;

/* ---- Tag constants ---- */
#define VTAG_MASK  ((val_t)0x3)
#define VTAG_PTR   ((val_t)0x0)
#define VTAG_FIX   ((val_t)0x1)
#define VTAG_CHR   ((val_t)0x2)
#define VTAG_IMM   ((val_t)0x3)

/* ---- Immediate sentinels (all have VTAG_IMM in low 2 bits) ---- */
#define V_FALSE  ((val_t)0x03)   /* #f              */
#define V_TRUE   ((val_t)0x07)   /* #t              */
#define V_NIL    ((val_t)0x0B)   /* '() empty list  */
#define V_VOID   ((val_t)0x0F)   /* unspecified     */
#define V_EOF    ((val_t)0x13)   /* eof-object      */
#define V_UNDEF  ((val_t)0x17)   /* unassigned slot */

/* ---- Tag extraction ---- */
#define vtag(v)         ((v) & VTAG_MASK)
#define vis_ptr(v)      (vtag(v) == VTAG_PTR && (v) != 0)
#define vis_fixnum(v)   (vtag(v) == VTAG_FIX)
#define vis_char(v)     (vtag(v) == VTAG_CHR)
#define vis_imm(v)      (vtag(v) == VTAG_IMM)

/* ---- Boolean ---- */
#define vis_bool(v)     ((v) == V_TRUE || (v) == V_FALSE)
#define vis_false(v)    ((v) == V_FALSE)
#define vis_true(v)     ((v) != V_FALSE)
#define vbool(b)        ((b) ? V_TRUE : V_FALSE)

/* ---- Other immediates ---- */
#define vis_nil(v)      ((v) == V_NIL)
#define vis_void(v)     ((v) == V_VOID)
#define vis_eof(v)      ((v) == V_EOF)

/* ---- Fixnum ---- */
#define vfix(n)         (((val_t)((uintptr_t)(intptr_t)(n) << 2)) | VTAG_FIX)
#define vunfix(v)       ((intptr_t)(v) >> 2)
#define FIXNUM_MAX      ((intptr_t)((UINTPTR_MAX >> 3) >> 1))
#define FIXNUM_MIN      (-FIXNUM_MAX - 1)
#define in_fixnum_range(n) ((intptr_t)(n) >= FIXNUM_MIN && (intptr_t)(n) <= FIXNUM_MAX)

/* ---- Character ---- */
#define vchr(c)         (((val_t)(uint32_t)(c) << 8) | VTAG_CHR)
#define vunchr(v)       ((uint32_t)((v) >> 8))

/* ---- Heap pointer ---- */
#define vptr(p)         ((val_t)(uintptr_t)(p))
#define vunptr(T, v)    ((T *)(uintptr_t)(v))

#endif /* CURRY_VALUE_H */
