#ifndef CURRY_OPCODE_H
#define CURRY_OPCODE_H

/*
 * Curry VM — stack-based bytecode instruction set
 *
 * Operand conventions:
 *   A  = uint8_t  immediate index (0–255)
 *   B  = uint16_t immediate index (0–65535, encoded as two bytes)
 *   All multi-byte operands are little-endian.
 *
 * Stack discipline:
 *   Instructions pop their inputs and push their result.
 *   The stack holds val_t values (same tagged union as the rest of Curry).
 */

typedef enum {
    /* ── Constants ──────────────────────────────────────────────────── */
    OP_CONST,       /* A: push constants[A]                             */
    OP_CONST_W,     /* B: push constants[B]  (wide, 2-byte index)       */
    OP_TRUE,        /* push #t                                           */
    OP_FALSE,       /* push #f                                           */
    OP_NIL,         /* push '()                                          */
    OP_VOID,        /* push void                                         */

    /* ── Local variables (current call frame) ───────────────────────── */
    OP_LOAD_LOCAL,  /* A: push frame->locals[A]                         */
    OP_STORE_LOCAL, /* A: frame->locals[A] = pop()  (value stays)       */

    /* ── Global variables ───────────────────────────────────────────── */
    OP_LOAD_GLOBAL, /* A: push global binding for constants[A] (symbol) */
    OP_STORE_GLOBAL,/* A: set global binding for constants[A] = pop()   */
    OP_DEF_GLOBAL,  /* A: define global constants[A] = pop()            */
    OP_DEFINED_GLOBAL, /* A: push #t if constants[A] (symbol) is bound in
                            TARGET_ENV, #f otherwise -- the non-raising
                            counterpart to OP_LOAD_GLOBAL, backing
                            (defined? sym) for a symbol that isn't a
                            compile-time-resolvable local/upvalue (see
                            compile_defined_p, compiler.c). No inline
                            cache: unlike OP_LOAD_GLOBAL this isn't a
                            hot-path lookup, and caching a *miss* would
                            need its own invalidation story (a global
                            that's *since become* bound), not worth it
                            for what's normally an occasional feature-
                            detection check.                          */

    /* ── Upvalue / closure captures ─────────────────────────────────── */
    OP_LOAD_UP,     /* A: push current closure's upvalue[A]             */
    OP_STORE_UP,    /* A: current closure's upvalue[A] = pop()          */

    /* ── Stack manipulation ─────────────────────────────────────────── */
    OP_POP,         /* discard top of stack                              */
    OP_DUP,         /* duplicate top of stack                            */
    OP_SWAP,        /* swap top two stack values                         */
    OP_SLIDE,       /* A: move TOS past A items below it (scope cleanup) */

    /* ── Arithmetic (numeric tower, same semantics as num_add etc.) ─── */
    OP_ADD,         /* push num_add(pop(), pop())   (order: b=pop a=pop) */
    OP_SUB,         /* push num_sub(a, b)                                */
    OP_MUL,         /* push num_mul(pop(), pop())                        */
    OP_DIV,         /* push num_div(a, b)                                */
    OP_NEG,         /* push num_neg(pop())                               */
    OP_ABS,         /* push num_abs(pop())                               */
    OP_EXPT,        /* push num_expt(a, b)                               */

    /* ── Comparison ─────────────────────────────────────────────────── */
    OP_EQ,          /* push num_eq(a,b)  → #t/#f                        */
    OP_LT,          /* push num_lt(a,b)                                  */
    OP_LE,          /* push num_le(a,b)                                  */
    OP_GT,          /* push num_gt(a,b)                                  */
    OP_GE,          /* push num_ge(a,b)                                  */
    OP_NUMEQ,       /* push (= a b) — numeric equality                  */

    /* ── Identity / equivalence ─────────────────────────────────────── */
    OP_EQV,         /* push scm_eqv(a,b)                                */
    OP_EQUAL,       /* push scm_equal(a,b)                              */
    OP_NOT,         /* push (not pop())                                  */

    /* ── Pairs / lists ──────────────────────────────────────────────── */
    OP_CONS,        /* push cons(a, b)                                   */
    OP_CAR,         /* push car(pop())                                   */
    OP_CDR,         /* push cdr(pop())                                   */
    OP_SETCAR,      /* set-car!(pair, val) — both popped                 */
    OP_SETCDR,      /* set-cdr!(pair, val)                               */
    OP_NULLP,       /* push (null? pop())                                */
    OP_PAIRP,       /* push (pair? pop())                                */

    /* ── Strings / chars ────────────────────────────────────────────── */
    OP_STRINGLEN,   /* push (string-length pop())                        */
    OP_STRINGREF,   /* push (string-ref s i)                             */
    OP_CHARTOFIX,   /* push (char->integer pop())                        */
    OP_FIXTOCHAR,   /* push (integer->char pop())                        */

    /* ── Type predicates ────────────────────────────────────────────── */
    OP_NUMBERP,
    OP_STRINGP,
    OP_SYMBOLP,
    OP_CHARP,
    OP_BOOLP,
    OP_PROCP,
    OP_VECTORP,

    /* ── Vectors ────────────────────────────────────────────────────── */
    OP_MAKEVEC,     /* A: push make-vector(pop_len, pop_fill)            */
    OP_VECREF,      /* push vector-ref(vec, idx)                         */
    OP_VECSET,      /* vector-set!(vec, idx, val)                        */
    OP_VECLEN,      /* push vector-length(pop())                         */

    /* ── Control flow ───────────────────────────────────────────────── */
    OP_JUMP,        /* B: unconditional jump to offset B (signed)        */
    OP_JUMP_FALSE,  /* B: jump if pop() is #f                            */
    OP_JUMP_TRUE,   /* B: jump if pop() is not #f                        */

    /* ── Calls ──────────────────────────────────────────────────────── */
    OP_CALL,        /* A: call top-of-stack with A args below it         */
    OP_TAIL_CALL,   /* A: tail call (reuses current frame)               */
    OP_RETURN,      /* return top of stack to caller                     */
    OP_SELF_TAIL_CALL, /* A: tail-call the CURRENTLY EXECUTING closure with
                         A args already on the stack (no callee pushed) --
                         compiler-proven self-recursion only (named-let
                         loops whose own name is never set!'d); see
                         compile_call's own comment in compiler.c for the
                         safety argument                                  */
    OP_CALL_GLOBAL,      /* A: constants[A] const-pool index of the global
                            symbol to look up (via the same cache
                            OP_LOAD_GLOBAL uses) and call; B: argc, args
                            already on the stack, no callee pushed        */
    OP_TAIL_CALL_GLOBAL, /* same as OP_CALL_GLOBAL but tail (reuses frame) */
    OP_TREE_EVAL_CACHED, /* A: constants[A] holds a raw form still punted to
                            the tree-walker (import/define-library/library
                            -- see compiler.c's tree-eval punt block);
                            chunk->tree_eval_cache[A] memoizes the result
                            of already having run it through eval() once,
                            so repeated executions of this exact bytecode
                            site (e.g. an import nested inside a function
                            called from a loop) only actually evaluate it
                            the first time. See chunk.h's Chunk::
                            tree_eval_cache comment for why this cache is
                            deliberately not .scc-persisted             */

    /* ── Closures ───────────────────────────────────────────────────── */
    OP_CLOSURE,     /* A: push new closure wrapping constants[A] (Chunk*)
                         followed by A upvalue capture instructions      */
    OP_CLOSE_UP,    /* A: close open upvalue for frame->slots[A] (no pop)*/

    /* ── apply / values ─────────────────────────────────────────────── */
    OP_APPLY,       /* (apply f args-list) — f and list on stack         */
    OP_VALUES,      /* A: bundle A values into a multiple-values object  */
    OP_VALUES_REF,  /* A: pop TOS (a multiple-values object or a plain
                         single value); push component A. If TOS is a
                         T_VALUES object, requires A < its count; if it's
                         a plain value, requires A == 0 (a single value
                         behaves as if it were a one-element values
                         object). Raises on an out-of-range A either way
                         -- the read-side counterpart OP_VALUES never
                         needed: OP_VALUES only ever BUILDS a values
                         object (used by (values ...) itself), nothing
                         previously needed to pull one back apart other
                         than OP_CALL_WITH_VALUES's own inline unpack
                         (vm.c), which hands the pieces straight to a
                         call rather than leaving them addressable on the
                         stack. compile_define_values (compiler.c) is the
                         first caller: define-values needs each bound
                         name's value individually, stored one at a time
                         via the same global/local dispatch an ordinary
                         define uses, which OP_CALL_WITH_VALUES's
                         call-shaped unpack can't provide.            */
    OP_CALL_WITH_VALUES, /* consumer, thunk on stack                     */
    OP_TAIL_CALL_WITH_VALUES, /* consumer, thunk on stack; tail-calls the
                                  consumer, reusing the current frame if
                                  it's a BcClosure (same idea as
                                  OP_TAIL_CALL vs OP_CALL)                */

    /* ── Exception handling ─────────────────────────────────────────── */
    OP_PUSH_HANDLER,/* B: push exception handler, jump offset B on exn   */
    OP_POP_HANDLER, /* pop exception handler frame                       */
    OP_RAISE,       /* raise top of stack as exception                   */

    /* ── I/O ────────────────────────────────────────────────────────── */
    OP_DISPLAY,     /* display pop() to current-output-port              */
    OP_WRITE,       /* write pop() to current-output-port                */
    OP_NEWLINE,     /* newline to current-output-port                    */

    /* ── Tail markers ───────────────────────────────────────────────── */
    OP_NOP,         /* no-op (for alignment / patching)                  */

    OP_COUNT        /* sentinel — number of opcodes                      */
} OpCode;

/* Human-readable names, indexed by OpCode */
extern const char *opcode_name[OP_COUNT];

#endif /* CURRY_OPCODE_H */
