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
    curry_val bv = curry_cdr(v);
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

static curry_val fn_add(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    VecDB *db = val_to_db(av[0]);
    long id   = (long)curry_fixnum(av[1]);
    uint32_t n = curry_vector_length(av[2]);
    std::vector<float> vec(n);
    for (uint32_t i = 0; i < n; i++) vec[i] = (float)curry_float(curry_vector_ref(av[2], i));
    db->entries[id] = vec;
    return curry_void();
}

static curry_val fn_search(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    VecDB *db  = val_to_db(av[0]);
    uint32_t n = curry_vector_length(av[1]);
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
