/*
 * curry_ldap — LDAP/LDAPS client module.
 *
 * Scheme API:
 *   (ldap-connect host port use-tls?)        -> connection
 *   (ldap-bind! conn dn password)            -> void
 *   (ldap-search conn base scope filter attrs) -> list-of-entries
 *     scope: 'base | 'one | 'sub
 *     attrs: list of strings, or #f for all
 *     entry:  (dn . alist)  where alist is ((attr-name . (val ...)) ...)
 *   (ldap-add! conn dn attrs)                -> void
 *   (ldap-modify! conn dn mods)              -> void
 *     mods: list of (op attr val...)  op: 'add|'delete|'replace
 *   (ldap-delete! conn dn)                   -> void
 *   (ldap-close! conn)                       -> void
 *   (ldap-set-option! conn option value)     -> void
 *     options: 'protocol-version | 'timeout | 'tls-require-cert
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <ldap.h>

/* ---- Handle packing ---- */

static curry_val ldap_to_val(LDAP *ld) {
    curry_val bv = curry_make_bytevector(sizeof(LDAP *), 0);
    for (size_t i = 0; i < sizeof(LDAP *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&ld)[i]);
    return curry_make_pair(curry_make_symbol("ldap-conn"), bv);
}

static LDAP *val_to_ldap(curry_val v) {
    if (!curry_is_pair(v) || !curry_is_symbol(curry_car(v)))
        curry_error("ldap: not a connection handle");
    curry_val bv = curry_cdr(v);
    LDAP *ld;
    for (size_t i = 0; i < sizeof(LDAP *); i++)
        ((uint8_t *)&ld)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return ld;
}

/* ---- connect ---- */

static curry_val fn_ldap_connect(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *host    = curry_string(av[0]);
    int         port    = (int)curry_fixnum(av[1]);
    bool        use_tls = curry_bool(av[2]);

    char uri[512];
    snprintf(uri, sizeof(uri), "%s://%s:%d",
             use_tls ? "ldaps" : "ldap", host, port);

    LDAP *ld = NULL;
    int rc = ldap_initialize(&ld, uri);
    if (rc != LDAP_SUCCESS)
        curry_error("ldap-connect: %s", ldap_err2string(rc));

    int ver = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &ver);

    if (use_tls) {
        int tls_val = LDAP_OPT_X_TLS_DEMAND;
        ldap_set_option(ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &tls_val);
    }

    return ldap_to_val(ld);
}

/* ---- bind ---- */

static curry_val fn_ldap_bind(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    LDAP       *ld  = val_to_ldap(av[0]);
    const char *dn  = curry_string(av[1]);
    const char *pw  = curry_string(av[2]);

    struct berval cred;
    cred.bv_val = (char *)pw;
    cred.bv_len = strlen(pw);

    int rc = ldap_sasl_bind_s(ld, dn, LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);
    if (rc != LDAP_SUCCESS)
        curry_error("ldap-bind!: %s (dn=%s)", ldap_err2string(rc), dn);

    return curry_void();
}

/* ---- search ---- */

/* Escape a string for safe embedding in an LDAP filter value (RFC 4515).
 * Returns a malloc'd string the caller must free. */
static char *ldap_escape_filter_value(const char *s) {
    /* Worst case: every byte expands to \XX (3 chars) */
    size_t len = strlen(s);
    char *out = malloc(len * 3 + 1);
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        /* RFC 4515: escape NUL, '(', ')', '*', '\' */
        if (c == '\0' || c == '(' || c == ')' || c == '*' || c == '\\') {
            p += sprintf(p, "\\%02x", c);
        } else {
            *p++ = (char)c;
        }
    }
    *p = '\0';
    return out;
}

static int scope_from_sym(curry_val sym) {
    if (!curry_is_symbol(sym)) curry_error("ldap-search: scope must be a symbol");
    const char *s = curry_symbol(sym);
    if (strcmp(s, "base") == 0) return LDAP_SCOPE_BASE;
    if (strcmp(s, "one")  == 0) return LDAP_SCOPE_ONELEVEL;
    if (strcmp(s, "sub")  == 0) return LDAP_SCOPE_SUBTREE;
    curry_error("ldap-search: unknown scope '%s' (use 'base, 'one, or 'sub)", s);
}

static char **attrs_from_val(curry_val v) {
    if (curry_is_bool(v) && !curry_bool(v)) return NULL;  /* #f → all attrs */
    int n = 0;
    curry_val tmp = v;
    while (!curry_is_nil(tmp)) { n++; tmp = curry_cdr(tmp); }
    char **arr = malloc((size_t)(n + 1) * sizeof(char *));
    for (int i = 0; i < n; i++) {
        arr[i] = (char *)curry_string(curry_car(v));
        v = curry_cdr(v);
    }
    arr[n] = NULL;
    return arr;
}

static curry_val fn_ldap_search(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    LDAP       *ld     = val_to_ldap(av[0]);
    const char *base   = curry_string(av[1]);
    int         scope  = scope_from_sym(av[2]);
    const char *filter = curry_string(av[3]);
    char      **attrs  = attrs_from_val(av[4]);

    LDAPMessage *result = NULL;
    int rc = ldap_search_ext_s(ld, base, scope, filter, attrs, 0,
                               NULL, NULL, NULL, LDAP_NO_LIMIT, &result);
    if (attrs) free(attrs);

    if (rc != LDAP_SUCCESS) {
        if (result) ldap_msgfree(result);
        curry_error("ldap-search: %s", ldap_err2string(rc));
    }

    /* Build list of (dn . alist) */
    curry_val entries = curry_nil();
    for (LDAPMessage *e = ldap_first_entry(ld, result); e;
         e = ldap_next_entry(ld, e)) {

        char *dn = ldap_get_dn(ld, e);
        curry_val alist = curry_nil();

        BerElement *ber = NULL;
        for (char *attr = ldap_first_attribute(ld, e, &ber); attr;
             attr = ldap_next_attribute(ld, e, ber)) {

            struct berval **vals = ldap_get_values_len(ld, e, attr);
            curry_val vlist = curry_nil();
            if (vals) {
                for (int i = ldap_count_values_len(vals) - 1; i >= 0; i--) {
                    curry_val s = curry_make_string(vals[i]->bv_val);
                    vlist = curry_make_pair(s, vlist);
                }
                ldap_value_free_len(vals);
            }
            curry_val pair = curry_make_pair(curry_make_symbol(attr), vlist);
            alist = curry_make_pair(pair, alist);
            ldap_memfree(attr);
        }
        if (ber) ber_free(ber, 0);

        curry_val entry = curry_make_pair(curry_make_string(dn), alist);
        entries = curry_make_pair(entry, entries);
        ldap_memfree(dn);
    }
    ldap_msgfree(result);
    return entries;
}

/* ---- close ---- */

static curry_val fn_ldap_close(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    LDAP *ld = val_to_ldap(av[0]);
    ldap_unbind_ext_s(ld, NULL, NULL);
    /* Zero the handle so accidental reuse fails fast */
    curry_val bv = curry_cdr(av[0]);
    for (uint32_t i = 0; i < (uint32_t)sizeof(LDAP *); i++)
        curry_bytevector_set(bv, i, 0);
    return curry_void();
}

/* ---- set-option! ---- */

static curry_val fn_ldap_set_option(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    LDAP       *ld  = val_to_ldap(av[0]);
    const char *opt = curry_symbol(av[1]);
    curry_val   val = av[2];

    if (strcmp(opt, "protocol-version") == 0) {
        int v = (int)curry_fixnum(val);
        ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &v);
    } else if (strcmp(opt, "timeout") == 0) {
        struct timeval tv;
        tv.tv_sec  = (long)curry_fixnum(val);
        tv.tv_usec = 0;
        ldap_set_option(ld, LDAP_OPT_TIMEOUT, &tv);
    } else if (strcmp(opt, "tls-require-cert") == 0) {
        int v = curry_bool(val) ? LDAP_OPT_X_TLS_DEMAND : LDAP_OPT_X_TLS_NEVER;
        ldap_set_option(ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &v);
    } else {
        curry_error("ldap-set-option!: unknown option '%s'", opt);
    }
    return curry_void();
}

/* (ldap-escape-value str) → escaped string safe for embedding in a filter value */
static curry_val fn_ldap_escape_value(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    char *escaped = ldap_escape_filter_value(curry_string(av[0]));
    curry_val result = curry_make_string(escaped);
    free(escaped);
    return result;
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "ldap-connect",       fn_ldap_connect,      3, 3, NULL);
    curry_define_fn(vm, "ldap-bind!",         fn_ldap_bind,         3, 3, NULL);
    curry_define_fn(vm, "ldap-search",        fn_ldap_search,       5, 5, NULL);
    curry_define_fn(vm, "ldap-close!",        fn_ldap_close,        1, 1, NULL);
    curry_define_fn(vm, "ldap-set-option!",   fn_ldap_set_option,   3, 3, NULL);
    curry_define_fn(vm, "ldap-escape-value",  fn_ldap_escape_value, 1, 1, NULL);
}
