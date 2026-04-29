/*
 * curry_crypto — base64, MD5, SHA-1, SHA-256, HMAC-SHA-256.
 *
 * base64 and MD5: pure C, no external dependencies.
 * SHA-1 / SHA-256 / HMAC: OpenSSL (libssl / libcrypto).
 *
 * Scheme API:
 *   (base64-encode bytevector)              -> string
 *   (base64-decode string)                  -> bytevector
 *   (md5 bytevector)                        -> bytevector  ; 16 bytes
 *   (md5-hex bytevector)                    -> string      ; 32 hex chars
 *   (sha256 bytevector)                     -> bytevector  ; 32 bytes
 *   (sha256-hex bytevector)                 -> string      ; 64 hex chars
 *   (sha1 bytevector)                       -> bytevector  ; 20 bytes
 *   (sha1-hex bytevector)                   -> string      ; 40 hex chars
 *   (hmac-sha256 key-bytevector data-bytevector) -> bytevector
 *
 * Strings are converted to/from UTF-8 bytevectors automatically.
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

/* ---- base64 ---- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static curry_val fn_base64_encode(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    uint32_t inlen = curry_bytevector_length(av[0]);
    size_t outlen = 4 * ((inlen + 2) / 3) + 1;
    char *out = malloc(outlen);
    if (!out) curry_error("base64-encode: out of memory");

    size_t j = 0;
    for (uint32_t i = 0; i < inlen; ) {
        uint32_t octet_a = i < inlen ? curry_bytevector_ref(av[0], i++) : 0;
        uint32_t octet_b = i < inlen ? curry_bytevector_ref(av[0], i++) : 0;
        uint32_t octet_c = i < inlen ? curry_bytevector_ref(av[0], i++) : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >>  6) & 0x3F];
        out[j++] = b64_table[ triple        & 0x3F];
    }
    /* Padding */
    uint32_t pad = inlen % 3;
    if (pad == 1) { out[j-2] = '='; out[j-1] = '='; }
    else if (pad == 2) { out[j-1] = '='; }
    out[j] = '\0';

    curry_val result = curry_make_string(out);
    free(out);
    return result;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static curry_val fn_base64_decode(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *in = curry_string(av[0]);
    size_t inlen = strlen(in);
    if (inlen % 4 != 0) curry_error("base64-decode: invalid input length");

    size_t outlen = (inlen / 4) * 3;
    if (inlen >= 1 && in[inlen-1] == '=') outlen--;
    if (inlen >= 2 && in[inlen-2] == '=') outlen--;

    curry_val bv = curry_make_bytevector((uint32_t)outlen, 0);
    size_t j = 0;
    for (size_t i = 0; i < inlen; i += 4) {
        int a = b64_val(in[i]),   b = b64_val(in[i+1]);
        int c = b64_val(in[i+2]), d = b64_val(in[i+3]);
        if (a < 0 || b < 0) curry_error("base64-decode: invalid character");
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                          ((uint32_t)(c >= 0 ? c : 0) << 6) | (uint32_t)(d >= 0 ? d : 0);
        if (j < outlen) curry_bytevector_set(bv, (uint32_t)j++, (triple >> 16) & 0xFF);
        if (j < outlen) curry_bytevector_set(bv, (uint32_t)j++, (triple >>  8) & 0xFF);
        if (j < outlen) curry_bytevector_set(bv, (uint32_t)j++,  triple        & 0xFF);
    }
    return bv;
}

/* ---- MD5 (RFC 1321, pure C) ---- */

typedef struct { uint32_t lo, hi; uint32_t a,b,c,d; uint8_t buf[64]; uint32_t block[16]; } MD5Ctx;

#define F(x,y,z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x,y,z) ((y) ^ ((z) & ((x) ^ (y))))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | ~(z)))
#define STEP(f,a,b,c,d,x,t,s) \
    (a) += f((b),(c),(d)) + (x) + (t); \
    (a) = (((a) << (s)) | ((a) >> (32-(s)))); \
    (a) += (b)

static void md5_block(MD5Ctx *ctx, const uint8_t *data) {
    uint32_t a = ctx->a, b = ctx->b, c = ctx->c, d = ctx->d, x[16];
    for (int i = 0; i < 16; i++)
        x[i] = (uint32_t)data[i*4] | ((uint32_t)data[i*4+1]<<8) |
                ((uint32_t)data[i*4+2]<<16) | ((uint32_t)data[i*4+3]<<24);

    STEP(F,a,b,c,d,x[ 0],0xd76aa478, 7); STEP(F,d,a,b,c,x[ 1],0xe8c7b756,12);
    STEP(F,c,d,a,b,x[ 2],0x242070db,17); STEP(F,b,c,d,a,x[ 3],0xc1bdceee,22);
    STEP(F,a,b,c,d,x[ 4],0xf57c0faf, 7); STEP(F,d,a,b,c,x[ 5],0x4787c62a,12);
    STEP(F,c,d,a,b,x[ 6],0xa8304613,17); STEP(F,b,c,d,a,x[ 7],0xfd469501,22);
    STEP(F,a,b,c,d,x[ 8],0x698098d8, 7); STEP(F,d,a,b,c,x[ 9],0x8b44f7af,12);
    STEP(F,c,d,a,b,x[10],0xffff5bb1,17); STEP(F,b,c,d,a,x[11],0x895cd7be,22);
    STEP(F,a,b,c,d,x[12],0x6b901122, 7); STEP(F,d,a,b,c,x[13],0xfd987193,12);
    STEP(F,c,d,a,b,x[14],0xa679438e,17); STEP(F,b,c,d,a,x[15],0x49b40821,22);

    STEP(G,a,b,c,d,x[ 1],0xf61e2562, 5); STEP(G,d,a,b,c,x[ 6],0xc040b340, 9);
    STEP(G,c,d,a,b,x[11],0x265e5a51,14); STEP(G,b,c,d,a,x[ 0],0xe9b6c7aa,20);
    STEP(G,a,b,c,d,x[ 5],0xd62f105d, 5); STEP(G,d,a,b,c,x[10],0x02441453, 9);
    STEP(G,c,d,a,b,x[15],0xd8a1e681,14); STEP(G,b,c,d,a,x[ 4],0xe7d3fbc8,20);
    STEP(G,a,b,c,d,x[ 9],0x21e1cde6, 5); STEP(G,d,a,b,c,x[14],0xc33707d6, 9);
    STEP(G,c,d,a,b,x[ 3],0xf4d50d87,14); STEP(G,b,c,d,a,x[ 8],0x455a14ed,20);
    STEP(G,a,b,c,d,x[13],0xa9e3e905, 5); STEP(G,d,a,b,c,x[ 2],0xfcefa3f8, 9);
    STEP(G,c,d,a,b,x[ 7],0x676f02d9,14); STEP(G,b,c,d,a,x[12],0x8d2a4c8a,20);

    STEP(H,a,b,c,d,x[ 5],0xfffa3942, 4); STEP(H,d,a,b,c,x[ 8],0x8771f681,11);
    STEP(H,c,d,a,b,x[11],0x6d9d6122,16); STEP(H,b,c,d,a,x[14],0xfde5380c,23);
    STEP(H,a,b,c,d,x[ 1],0xa4beea44, 4); STEP(H,d,a,b,c,x[ 4],0x4bdecfa9,11);
    STEP(H,c,d,a,b,x[ 7],0xf6bb4b60,16); STEP(H,b,c,d,a,x[10],0xbebfbc70,23);
    STEP(H,a,b,c,d,x[13],0x289b7ec6, 4); STEP(H,d,a,b,c,x[ 0],0xeaa127fa,11);
    STEP(H,c,d,a,b,x[ 3],0xd4ef3085,16); STEP(H,b,c,d,a,x[ 6],0x04881d05,23);
    STEP(H,a,b,c,d,x[ 9],0xd9d4d039, 4); STEP(H,d,a,b,c,x[12],0xe6db99e5,11);
    STEP(H,c,d,a,b,x[15],0x1fa27cf8,16); STEP(H,b,c,d,a,x[ 2],0xc4ac5665,23);

    STEP(I,a,b,c,d,x[ 0],0xf4292244, 6); STEP(I,d,a,b,c,x[ 7],0x432aff97,10);
    STEP(I,c,d,a,b,x[14],0xab9423a7,15); STEP(I,b,c,d,a,x[ 5],0xfc93a039,21);
    STEP(I,a,b,c,d,x[12],0x655b59c3, 6); STEP(I,d,a,b,c,x[ 3],0x8f0ccc92,10);
    STEP(I,c,d,a,b,x[10],0xffeff47d,15); STEP(I,b,c,d,a,x[ 1],0x85845dd1,21);
    STEP(I,a,b,c,d,x[ 8],0x6fa87e4f, 6); STEP(I,d,a,b,c,x[15],0xfe2ce6e0,10);
    STEP(I,c,d,a,b,x[ 6],0xa3014314,15); STEP(I,b,c,d,a,x[13],0x4e0811a1,21);
    STEP(I,a,b,c,d,x[ 4],0xf7537e82, 6); STEP(I,d,a,b,c,x[11],0xbd3af235,10);
    STEP(I,c,d,a,b,x[ 2],0x2ad7d2bb,15); STEP(I,b,c,d,a,x[ 9],0xeb86d391,21);

    ctx->a += a; ctx->b += b; ctx->c += c; ctx->d += d;
}

static void md5_init(MD5Ctx *ctx) {
    ctx->a=0x67452301; ctx->b=0xefcdab89; ctx->c=0x98badcfe; ctx->d=0x10325476;
    ctx->lo = ctx->hi = 0;
}

static void md5_update(MD5Ctx *ctx, const uint8_t *data, size_t len) {
    uint32_t saved_lo = ctx->lo;
    if ((ctx->lo = (saved_lo + (uint32_t)len) & 0x1fffffff) < saved_lo) ctx->hi++;
    ctx->hi += (uint32_t)(len >> 29);
    size_t used = saved_lo & 0x3f;
    if (used) {
        size_t avail = 64 - used;
        if (len < avail) { memcpy(ctx->buf + used, data, len); return; }
        memcpy(ctx->buf + used, data, avail);
        md5_block(ctx, ctx->buf);
        data += avail; len -= avail;
    }
    while (len >= 64) { md5_block(ctx, data); data += 64; len -= 64; }
    memcpy(ctx->buf, data, len);
}

static void md5_final(MD5Ctx *ctx, uint8_t *digest) {
    size_t used = ctx->lo & 0x3f;
    ctx->buf[used++] = 0x80;
    size_t avail = 64 - used;
    if (avail < 8) { memset(ctx->buf+used,0,avail); md5_block(ctx,ctx->buf); used=0; avail=64; }
    memset(ctx->buf+used, 0, avail-8);
    ctx->lo <<= 3;
    ctx->buf[56]=(uint8_t)ctx->lo;       ctx->buf[57]=(uint8_t)(ctx->lo>>8);
    ctx->buf[58]=(uint8_t)(ctx->lo>>16); ctx->buf[59]=(uint8_t)(ctx->lo>>24);
    ctx->buf[60]=(uint8_t)ctx->hi;       ctx->buf[61]=(uint8_t)(ctx->hi>>8);
    ctx->buf[62]=(uint8_t)(ctx->hi>>16); ctx->buf[63]=(uint8_t)(ctx->hi>>24);
    md5_block(ctx, ctx->buf);
    uint32_t vals[4] = {ctx->a, ctx->b, ctx->c, ctx->d};
    for (int i = 0; i < 4; i++) {
        digest[i*4]   = (uint8_t)vals[i];       digest[i*4+1] = (uint8_t)(vals[i]>>8);
        digest[i*4+2] = (uint8_t)(vals[i]>>16); digest[i*4+3] = (uint8_t)(vals[i]>>24);
    }
}

static curry_val fn_md5(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    MD5Ctx ctx; md5_init(&ctx);
    uint32_t n = curry_bytevector_length(av[0]);
    uint8_t *tmp = malloc(n);
    for (uint32_t i = 0; i < n; i++) tmp[i] = curry_bytevector_ref(av[0], i);
    md5_update(&ctx, tmp, n); free(tmp);
    uint8_t digest[16]; md5_final(&ctx, digest);
    curry_val bv = curry_make_bytevector(16, 0);
    for (int i = 0; i < 16; i++) curry_bytevector_set(bv, (uint32_t)i, digest[i]);
    return bv;
}

static curry_val fn_md5_hex(int ac, curry_val *av, void *ud) {
    curry_val bv = fn_md5(ac, av, ud);
    char hex[33];
    for (int i = 0; i < 16; i++)
        snprintf(hex + i*2, 3, "%02x", curry_bytevector_ref(bv, (uint32_t)i));
    return curry_make_string(hex);
}

/* ---- SHA-1, SHA-256, HMAC via OpenSSL EVP ---- */

static curry_val hash_via_evp(const EVP_MD *md, curry_val bv_in) {
    uint32_t inlen = curry_bytevector_length(bv_in);
    uint8_t *in = malloc(inlen);
    for (uint32_t i = 0; i < inlen; i++) in[i] = curry_bytevector_ref(bv_in, i);

    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, md, NULL);
    EVP_DigestUpdate(ctx, in, inlen);
    EVP_DigestFinal_ex(ctx, digest, &dlen);
    EVP_MD_CTX_free(ctx);
    free(in);

    curry_val out = curry_make_bytevector(dlen, 0);
    for (unsigned int i = 0; i < dlen; i++) curry_bytevector_set(out, i, digest[i]);
    return out;
}

static curry_val bv_to_hex(curry_val bv) {
    uint32_t n = curry_bytevector_length(bv);
    char *hex = malloc(n*2 + 1);
    for (uint32_t i = 0; i < n; i++)
        snprintf(hex + i*2, 3, "%02x", curry_bytevector_ref(bv, i));
    hex[n*2] = '\0';
    curry_val s = curry_make_string(hex);
    free(hex);
    return s;
}

static curry_val fn_sha1(int ac, curry_val *av, void *ud)    { (void)ud;(void)ac; return hash_via_evp(EVP_sha1(),   av[0]); }
static curry_val fn_sha1_hex(int ac, curry_val *av, void *ud){ (void)ud; return bv_to_hex(fn_sha1(ac,av,ud)); }
static curry_val fn_sha256(int ac, curry_val *av, void *ud)  { (void)ud;(void)ac; return hash_via_evp(EVP_sha256(), av[0]); }
static curry_val fn_sha256_hex(int ac,curry_val *av,void *ud){ (void)ud; return bv_to_hex(fn_sha256(ac,av,ud)); }

static curry_val fn_hmac_sha256(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    uint32_t klen = curry_bytevector_length(av[0]);
    uint32_t dlen = curry_bytevector_length(av[1]);
    uint8_t *key  = malloc(klen), *data = malloc(dlen);
    for (uint32_t i = 0; i < klen; i++) key[i]  = curry_bytevector_ref(av[0], i);
    for (uint32_t i = 0; i < dlen; i++) data[i] = curry_bytevector_ref(av[1], i);

    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int outlen = 0;
    HMAC(EVP_sha256(), key, (int)klen, data, dlen, digest, &outlen);
    free(key); free(data);

    curry_val out = curry_make_bytevector(outlen, 0);
    for (unsigned int i = 0; i < outlen; i++) curry_bytevector_set(out, i, digest[i]);
    return out;
}

/* Convert a string to a UTF-8 bytevector (for hashing strings directly) */
static curry_val fn_string_to_utf8(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *s = curry_string(av[0]);
    uint32_t n = (uint32_t)strlen(s);
    curry_val bv = curry_make_bytevector(n, 0);
    for (uint32_t i = 0; i < n; i++) curry_bytevector_set(bv, i, (uint8_t)s[i]);
    return bv;
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "base64-encode",  fn_base64_encode,  1, 1, NULL);
    curry_define_fn(vm, "base64-decode",  fn_base64_decode,  1, 1, NULL);
    curry_define_fn(vm, "md5",            fn_md5,            1, 1, NULL);
    curry_define_fn(vm, "md5-hex",        fn_md5_hex,        1, 1, NULL);
    curry_define_fn(vm, "sha1",           fn_sha1,           1, 1, NULL);
    curry_define_fn(vm, "sha1-hex",       fn_sha1_hex,       1, 1, NULL);
    curry_define_fn(vm, "sha256",         fn_sha256,         1, 1, NULL);
    curry_define_fn(vm, "sha256-hex",     fn_sha256_hex,     1, 1, NULL);
    curry_define_fn(vm, "hmac-sha256",    fn_hmac_sha256,    2, 2, NULL);
    curry_define_fn(vm, "string->utf8",   fn_string_to_utf8, 1, 1, NULL);
}
