#ifndef CURRY_QUANTUM_H
#define CURRY_QUANTUM_H

/*
 * Quantum superposition as a first-class value type.
 *
 * A quantum value represents a superposition of classical values with
 * complex probability amplitudes:
 *
 *   |ψ⟩ = Σᵢ αᵢ|xᵢ⟩    where Σᵢ |αᵢ|² = 1
 *
 * Operations:
 *   (superpose '((amp1 . val1) (amp2 . val2) ...))  → T_QUANTUM
 *   (observe q)          → collapse to one value (probabilistic)
 *   (quantum-map f q)    → apply f to each classical state
 *   (quantum-states q)   → list of (amplitude . value) pairs
 *
 * Arithmetic on quantum values maps the operation over each state:
 *   (+ q 3) → apply (+ 3) to each branch
 *   (+ q1 q2) → tensor superposition of both
 */

#include "value.h"
#include <stdbool.h>

void  quantum_init(void);

/* Build from list of (complex-amplitude . value) pairs; normalizes */
val_t quantum_from_pairs(val_t pair_list);

/* Equal superposition of a list of values */
val_t quantum_uniform(val_t value_list);

/* Probabilistic collapse — uses current random state */
val_t quantum_observe(val_t q);

/* Apply a procedure (val_t closure) to each classical state */
val_t quantum_map_fn(val_t q, val_t (*fn)(val_t, void *), void *ud);

/* Arithmetic with a scalar */
val_t quantum_add_scalar(val_t q, val_t s);
val_t quantum_sub_scalar(val_t q, val_t s);
val_t quantum_mul_scalar(val_t q, val_t s);
val_t quantum_div_scalar(val_t q, val_t s);

/* Combine two quantum values (superposition union) */
val_t quantum_superpose(val_t a, val_t b);

/* Accessors */
int   quantum_n(val_t q);
val_t quantum_amp(val_t q, int i);
val_t quantum_val(val_t q, int i);

/* Return as list of (amplitude . value) pairs */
val_t quantum_to_list(val_t q);

/* Display */
void  quantum_write(val_t q, val_t port);

#endif /* CURRY_QUANTUM_H */
