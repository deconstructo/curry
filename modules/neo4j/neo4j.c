/*
 * curry_neo4j — Neo4j graph database module for Curry Scheme.
 *
 * Uses libneo4j-client (https://github.com/cleishm/libneo4j-client).
 *
 * Scheme API:
 *   (neo4j-connect url)                -> connection
 *   (neo4j-disconnect conn)            -> void
 *   (neo4j-run conn cypher [params])   -> list of row alists
 *   (neo4j-begin-tx conn)              -> tx
 *   (neo4j-commit tx)                  -> void
 *   (neo4j-rollback tx)                -> void
 *
 * Cypher results map:
 *   Integer  -> fixnum
 *   Float    -> flonum
 *   String   -> string
 *   Boolean  -> bool
 *   List     -> vector
 *   Map      -> alist
 *   Node     -> alist with (id labels properties)
 *   Relation -> alist with (id type start end properties)
 *
 * Build with -DBUILD_MODULE_NEO4J=ON when libneo4j-client is available.
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>

/* Stub implementation — requires libneo4j-client to compile fully */

static curry_val fn_connect(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    fprintf(stderr, "[neo4j] neo4j-connect: libneo4j-client not linked (stub)\n");
    return curry_make_pair(curry_make_symbol("neo4j-conn"),
                           curry_make_string(curry_string(av[0])));
}

static curry_val fn_disconnect(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; (void)av;
    return curry_void();
}

static curry_val fn_run(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    fprintf(stderr, "[neo4j] neo4j-run (stub): %s\n", curry_string(av[1]));
    return curry_nil();
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "neo4j-connect",    fn_connect,    1, 1, NULL);
    curry_define_fn(vm, "neo4j-disconnect", fn_disconnect, 1, 1, NULL);
    curry_define_fn(vm, "neo4j-run",        fn_run,        2, 3, NULL);
}
