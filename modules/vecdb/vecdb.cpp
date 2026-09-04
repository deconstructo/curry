/*
 * curry_vecdb — Vector database module for Curry Scheme.
 *
 * Uses usearch (https://github.com/unum-cloud/usearch) for approximate
 * nearest-neighbor search with HNSW graphs.
 *
 * Scheme API:
 *   (vecdb-make dimensions [metric])  -> db   ; metric: 'cosine | 'l2 | 'ip
 *   (vecdb-add db id vector)           -> void ; id = fixnum, vector = vector of floats
 *   (vecdb-search db query k)          -> list of (id . distance) pairs
 *   (vecdb-remove db id)               -> void
 *   (vecdb-size db)                    -> integer
 *   (vecdb-save db path)               -> void
 *   (vecdb-load path)                  -> db
 *
 * The vector type is a Scheme vector of flonums (inexact reals).
 * Typical use:
 *   (define db (vecdb-make 384 'cosine))
 *   (vecdb-add db 0 (embedding "hello world"))
 *   (vecdb-search db (embedding "hi there") 5)
 *
 * Build with -DBUILD_MODULE_VECDB=ON.
 */

#include <curry.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <map>

/* Brute-force cosine similarity stub (works without usearch) */

struct VecDB {
    int dims;
    int metric; /* 0=cosine, 1=l2, 2=ip */
    std::map<long, std::vector<float>> entries;
};

static curry_val db_to_val(VecDB *db) {
    curry_val bv = curry_make_bytevector(sizeof(VecDB *), 0);
    for (size_t i = 0; i < sizeof(VecDB *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&db)[i]);
    return curry_make_pair(curry_make_symbol("vecdb"), bv);
}

static VecDB *val_to_db(curry_val v) {
    /* Issue #165: this checked NOTHING at all -- not even a tag, let
     * alone that the cdr was really a pointer-holding bytevector. */
    if (!curry_is_pair(v) || !curry_is_symbol(curry_car(v)) ||
        strcmp(curry_symbol(curry_car(v)), "vecdb") != 0)
        curry_error("vecdb: not a vector db handle");
    curry_val bv = curry_cdr(v);
    if (!curry_is_bytevector(bv) || curry_bytevector_length(bv) != sizeof(VecDB *))
        curry_error("vecdb: not a vector db handle");
    VecDB *db;
    for (size_t i = 0; i < sizeof(VecDB *); i++)
        ((uint8_t *)&db)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return db;
}

static float cosine_dist(const float *a, const float *b, int n) {
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) { dot += a[i]*b[i]; na += a[i]*a[i]; nb += b[i]*b[i]; }
    float denom = sqrtf(na) * sqrtf(nb);
    return denom > 0 ? 1.0f - dot/denom : 1.0f;
}

static float l2_dist(const float *a, const float *b, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) { float d = a[i]-b[i]; s += d*d; }
    return sqrtf(s);
}

extern "C" {

static curry_val fn_make(int ac, curry_val *av, void *ud) {
    (void)ud;
    int dims = (int)curry_fixnum(av[0]);
    int metric = 0; /* cosine default */
    if (ac > 1) {
        const char *m = curry_symbol(av[1]);
        if (!strcmp(m, "l2")) metric = 1;
        else if (!strcmp(m, "ip")) metric = 2;
    }
    VecDB *db = new VecDB();
    db->dims = dims; db->metric = metric;
    return db_to_val(db);
}

/* Issue #179: neither fn_add nor fn_search checked that the vector
 * argument was actually a vector at all before curry_vector_length/_ref
 * (an unchecked as_vec() cast, same class as #167's forged-image-vector
 * bug), nor that each ELEMENT was actually numeric before curry_float --
 * which is itself unchecked for anything but a fixnum/flonum (vfloat(v)
 * is a raw as_flo(v)->value cast), so e.g. a vector containing a string
 * or symbol would still wild-cast even after only checking the outer
 * vector. Confirmed reproducible SIGSEGV via
 * (vecdb-add db 1 42) and (vecdb-add db 1 (vector "x")). */
static void check_numeric_vector(curry_val v, const char *who) {
    if (!curry_is_vector(v)) curry_error("%s: not a vector", who);
    uint32_t n = curry_vector_length(v);
    for (uint32_t i = 0; i < n; i++) {
        curry_val e = curry_vector_ref(v, i);
        if (!curry_is_fixnum(e) && !curry_is_float(e))
            curry_error("%s: vector element %u is not a real number", who, i);
    }
}

static curry_val fn_add(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    VecDB *db = val_to_db(av[0]);
    long id   = (long)curry_fixnum(av[1]);
    check_numeric_vector(av[2], "vecdb-add");
    uint32_t n = curry_vector_length(av[2]);
    if ((int)n != db->dims)
        curry_error("vecdb-add: vector has %u dimensions, expected %d", n, db->dims);
    std::vector<float> vec(n);
    for (uint32_t i = 0; i < n; i++) vec[i] = (float)curry_float(curry_vector_ref(av[2], i));
    db->entries[id] = vec;
    return curry_void();
}

static curry_val fn_search(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    VecDB *db  = val_to_db(av[0]);
    check_numeric_vector(av[1], "vecdb-search");
    uint32_t n = curry_vector_length(av[1]);
    if ((int)n != db->dims)
        curry_error("vecdb-search: query vector has %u dimensions, expected %d", n, db->dims);
    std::vector<float> query(n);
    for (uint32_t i = 0; i < n; i++) query[i] = (float)curry_float(curry_vector_ref(av[1], i));
    int k = (int)curry_fixnum(av[2]);

    std::vector<std::pair<float, long>> scored;
    for (auto &kv : db->entries) {
        float dist = db->metric == 1
            ? l2_dist(query.data(), kv.second.data(), db->dims)
            : cosine_dist(query.data(), kv.second.data(), db->dims);
        scored.push_back({dist, kv.first});
    }
    /* Partial sort for top k */
    std::sort(scored.begin(), scored.end());
    if ((int)scored.size() > k) scored.resize((size_t)k);

    curry_val result = curry_nil();
    for (int i = (int)scored.size()-1; i >= 0; i--) {
        curry_val pair = curry_make_pair(curry_make_fixnum(scored[i].second),
                                        curry_make_float(scored[i].first));
        result = curry_make_pair(pair, result);
    }
    return result;
}

static curry_val fn_remove(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    VecDB *db = val_to_db(av[0]);
    db->entries.erase((long)curry_fixnum(av[1]));
    return curry_void();
}

static curry_val fn_size(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_fixnum((intptr_t)val_to_db(av[0])->entries.size());
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "vecdb-make",   fn_make,   1, 2, NULL);
    curry_define_fn(vm, "vecdb-add",    fn_add,    3, 3, NULL);
    curry_define_fn(vm, "vecdb-search", fn_search, 3, 3, NULL);
    curry_define_fn(vm, "vecdb-remove", fn_remove, 2, 2, NULL);
    curry_define_fn(vm, "vecdb-size",   fn_size,   1, 1, NULL);
}

} /* extern "C" */
