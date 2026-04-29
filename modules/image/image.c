/*
 * curry_image — PNG / JPEG / GIF image I/O for Curry Scheme.
 *
 * Images are represented as:
 *   #(width height channels bytevector)
 * where channels = 3 (RGB) or 4 (RGBA) and bytevector contains
 * width*height*channels bytes in row-major, top-to-bottom order.
 *
 * Scheme API:
 *   (image-load  path)                    -> image | #f
 *   (image-save  path image)              -> void
 *   (image-make  width height channels)   -> image  (zeroed)
 *   (image-width    image)                -> integer
 *   (image-height   image)                -> integer
 *   (image-channels image)                -> integer (3 or 4)
 *   (image-pixels   image)                -> bytevector
 *   (image-ref  image x y channel)        -> integer [0,255]
 *   (image-set! image x y channel value)  -> void
 *   (image-crop image x y width height)   -> image
 *   (image-scale image new-width new-height) -> image  (bilinear)
 *   (image-flip-horizontal image)         -> image
 *   (image-flip-vertical   image)         -> image
 *   (image-grayscale image)               -> image  (3-channel)
 *   (image-format path)                   -> symbol: png | jpeg | gif | unknown
 *
 * Compile-time feature flags (set by CMake):
 *   HAVE_PNG   — libpng is available
 *   HAVE_JPEG  — libjpeg is available
 *
 * GIF read is handled via a bundled single-file pure-C decoder (no deps).
 * GIF write is not supported (GIF patents aside, it's rare to need it).
 */

#include <curry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp on macOS / POSIX */
#include <stdint.h>

#ifdef HAVE_PNG
#  include <png.h>
#endif
#ifdef HAVE_JPEG
#  include <jpeglib.h>
#  include <setjmp.h>
#endif

/* ---- Image value helpers ---- */

/* image = #(width height channels bytevector) */
static curry_val make_image(uint32_t w, uint32_t h, uint32_t ch) {
    if (w == 0 || h == 0 || ch == 0 || ch > 4)
        curry_error("image: invalid dimensions %ux%u channels=%u", w, h, ch);
    /* Check for multiplication overflow before allocating */
    if (h > UINT32_MAX / w || (uint64_t)w * h > UINT32_MAX / ch)
        curry_error("image: dimensions too large %ux%u channels=%u", w, h, ch);
    curry_val bv = curry_make_bytevector(w * h * ch, 0);
    curry_val v  = curry_make_vector(4, curry_void());
    curry_vector_set(v, 0, curry_make_fixnum((intptr_t)w));
    curry_vector_set(v, 1, curry_make_fixnum((intptr_t)h));
    curry_vector_set(v, 2, curry_make_fixnum((intptr_t)ch));
    curry_vector_set(v, 3, bv);
    return v;
}

static void check_image(curry_val v) {
    if (!curry_is_fixnum(curry_vector_ref(v,0)))
        curry_error("image: not a valid image object");
}

static uint32_t img_w(curry_val v)   { return (uint32_t)curry_fixnum(curry_vector_ref(v,0)); }
static uint32_t img_h(curry_val v)   { return (uint32_t)curry_fixnum(curry_vector_ref(v,1)); }
static uint32_t img_ch(curry_val v)  { return (uint32_t)curry_fixnum(curry_vector_ref(v,2)); }
static curry_val img_bv(curry_val v) { return curry_vector_ref(v,3); }

static uint8_t px_get(curry_val img, uint32_t x, uint32_t y, uint32_t c) {
    uint32_t w = img_w(img), h = img_h(img), ch = img_ch(img);
    if (x >= w || y >= h || c >= ch)
        curry_error("image-ref: index out of bounds (%u,%u,%u) in %ux%u/%u image", x, y, c, w, h, ch);
    uint32_t idx = (y * w + x) * ch + c;
    return curry_bytevector_ref(img_bv(img), idx);
}

static void px_set(curry_val img, uint32_t x, uint32_t y, uint32_t c, uint8_t val) {
    uint32_t w = img_w(img), h = img_h(img), ch = img_ch(img);
    if (x >= w || y >= h || c >= ch)
        curry_error("image-set!: index out of bounds (%u,%u,%u) in %ux%u/%u image", x, y, c, w, h, ch);
    uint32_t idx = (y * w + x) * ch + c;
    curry_bytevector_set(img_bv(img), idx, val);
}

/* ---- Format detection ---- */

typedef enum { FMT_PNG, FMT_JPEG, FMT_GIF, FMT_UNKNOWN } ImgFmt;

static ImgFmt detect_format(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return FMT_UNKNOWN;
    dot++;
    if (!strcasecmp(dot, "png"))                          return FMT_PNG;
    if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) return FMT_JPEG;
    if (!strcasecmp(dot, "gif"))                          return FMT_GIF;
    /* Probe file magic */
    FILE *f = fopen(path, "rb");
    if (!f) return FMT_UNKNOWN;
    uint8_t hdr[8];
    size_t n = fread(hdr, 1, 8, f);
    fclose(f);
    if (n >= 8 && hdr[0]==0x89 && hdr[1]=='P' && hdr[2]=='N' && hdr[3]=='G') return FMT_PNG;
    if (n >= 3 && hdr[0]==0xFF && hdr[1]==0xD8 && hdr[2]==0xFF) return FMT_JPEG;
    if (n >= 6 && !memcmp(hdr, "GIF87a", 6)) return FMT_GIF;
    if (n >= 6 && !memcmp(hdr, "GIF89a", 6)) return FMT_GIF;
    return FMT_UNKNOWN;
}

/* ---- PNG I/O ---- */

#ifdef HAVE_PNG

static curry_val load_png(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return curry_make_bool(false);

    uint8_t sig[8];
    if (fread(sig, 1, 8, f) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(f); return curry_make_bool(false);
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(f); return curry_make_bool(false); }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(f); return curry_make_bool(false); }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f);
        return curry_make_bool(false);
    }

    png_init_io(png, f);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    uint32_t w  = png_get_image_width(png, info);
    uint32_t h  = png_get_image_height(png, info);
    int color   = png_get_color_type(png, info);
    int depth   = png_get_bit_depth(png, info);

    /* Normalise to 8-bit RGBA */
    if (depth == 16)             png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_RGB ||
        color == PNG_COLOR_TYPE_GRAY ||
        color == PNG_COLOR_TYPE_PALETTE) png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    if (color == PNG_COLOR_TYPE_GRAY ||
        color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    curry_val img = make_image(w, h, 4);
    uint8_t **rows = malloc(h * sizeof(uint8_t *));
    uint8_t *buf   = malloc(w * h * 4);
    for (uint32_t i = 0; i < h; i++) rows[i] = buf + i * w * 4;

    png_read_image(png, rows);

    curry_val bv = img_bv(img);
    for (uint32_t i = 0; i < w * h * 4; i++)
        curry_bytevector_set(bv, i, buf[i]);

    free(rows); free(buf);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(f);
    return img;
}

static void save_png(const char *path, curry_val img) {
    uint32_t w = img_w(img), h = img_h(img), ch = img_ch(img);

    FILE *f = fopen(path, "wb");
    if (!f) curry_error("image-save: cannot open %s for writing", path);

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info  = png_create_info_struct(png);

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(f);
        curry_error("image-save: PNG write error");
    }

    png_init_io(png, f);
    int color_type = (ch == 4) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
    png_set_IHDR(png, info, w, h, 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint8_t *row = malloc(w * ch);
    curry_val bv = img_bv(img);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t i = 0; i < w * ch; i++)
            row[i] = curry_bytevector_ref(bv, y * w * ch + i);
        png_write_row(png, row);
    }
    free(row);

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
}

#else  /* !HAVE_PNG */

static curry_val load_png(const char *path) {
    (void)path;
    curry_error("image: PNG support not compiled in (install libpng-dev)");
}
static void save_png(const char *path, curry_val img) {
    (void)path; (void)img;
    curry_error("image: PNG support not compiled in");
}

#endif

/* ---- JPEG I/O ---- */

#ifdef HAVE_JPEG

typedef struct {
    struct jpeg_error_mgr mgr;
    jmp_buf jmp;
} JpegErr;

static void jpeg_error_exit(j_common_ptr cinfo) {
    JpegErr *e = (JpegErr *)cinfo->err;
    longjmp(e->jmp, 1);
}

static curry_val load_jpeg(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return curry_make_bool(false);

    struct jpeg_decompress_struct cinfo;
    JpegErr jerr;
    cinfo.err = jpeg_std_error(&jerr.mgr);
    jerr.mgr.error_exit = jpeg_error_exit;

    if (setjmp(jerr.jmp)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        return curry_make_bool(false);
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    uint32_t w  = (uint32_t)cinfo.output_width;
    uint32_t h  = (uint32_t)cinfo.output_height;
    uint32_t ch = (uint32_t)cinfo.output_components;

    curry_val img = make_image(w, h, ch);
    curry_val bv  = img_bv(img);

    uint8_t *row = malloc(w * ch);
    for (uint32_t y = 0; y < h; y++) {
        jpeg_read_scanlines(&cinfo, &row, 1);
        for (uint32_t i = 0; i < w * ch; i++)
            curry_bytevector_set(bv, y * w * ch + i, row[i]);
    }
    free(row);

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
}

static void save_jpeg(const char *path, curry_val img, int quality) {
    uint32_t w = img_w(img), h = img_h(img), ch = img_ch(img);

    FILE *f = fopen(path, "wb");
    if (!f) curry_error("image-save: cannot open %s for writing", path);

    struct jpeg_compress_struct cinfo;
    JpegErr jerr;
    cinfo.err = jpeg_std_error(&jerr.mgr);
    jerr.mgr.error_exit = jpeg_error_exit;

    if (setjmp(jerr.jmp)) {
        jpeg_destroy_compress(&cinfo);
        fclose(f);
        curry_error("image-save: JPEG write error");
    }

    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width      = w;
    cinfo.image_height     = h;
    cinfo.input_components = (int)(ch >= 3 ? 3 : 1);
    cinfo.in_color_space   = (ch >= 3) ? JCS_RGB : JCS_GRAYSCALE;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    curry_val bv = img_bv(img);
    uint8_t *row = malloc(w * 3);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            row[x*3+0] = curry_bytevector_ref(bv, (y*w+x)*ch+0);
            row[x*3+1] = curry_bytevector_ref(bv, (y*w+x)*ch+1);
            row[x*3+2] = curry_bytevector_ref(bv, (y*w+x)*ch+2);
        }
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    free(row);

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(f);
}

#else  /* !HAVE_JPEG */

static curry_val load_jpeg(const char *path) {
    (void)path;
    curry_error("image: JPEG support not compiled in (install libjpeg-dev)");
}
static void save_jpeg(const char *path, curry_val img, int quality) {
    (void)path; (void)img; (void)quality;
    curry_error("image: JPEG support not compiled in");
}

#endif

/* ---- GIF decoder (pure C, no dependencies) ---- */
/* Supports GIF87a and GIF89a; returns first frame as RGB image. */

static curry_val load_gif(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return curry_make_bool(false);

    /* Read header */
    uint8_t hdr[13];
    if (fread(hdr, 1, 13, f) < 13 || memcmp(hdr, "GIF", 3)) {
        fclose(f); return curry_make_bool(false);
    }

    uint32_t w = (uint32_t)(hdr[6] | (hdr[7] << 8));
    uint32_t h = (uint32_t)(hdr[8] | (hdr[9] << 8));
    int flags  = hdr[10];
    int has_gct = (flags >> 7) & 1;
    int gct_size = has_gct ? (2 << (flags & 7)) : 0;

    /* Global colour table */
    uint8_t gct[768];
    memset(gct, 0, sizeof(gct));
    if (has_gct && fread(gct, 3, (size_t)gct_size, f) < (size_t)gct_size) {
        fclose(f); return curry_make_bool(false);
    }

    /* Skip extension blocks until we find image descriptor (0x2C) */
    uint8_t *lct = NULL;
    for (;;) {
        int introducer = fgetc(f);
        if (introducer == EOF) { fclose(f); return curry_make_bool(false); }
        if (introducer == 0x3B) { fclose(f); return curry_make_bool(false); } /* trailer */
        if (introducer == 0x21) { /* extension */
            fgetc(f); /* extension label */
            int bsize;
            while ((bsize = fgetc(f)) > 0)
                for (int i = 0; i < bsize; i++) fgetc(f);
        } else if (introducer == 0x2C) {
            break;
        }
    }

    /* Image descriptor */
    uint8_t idesc[9];
    if (fread(idesc, 1, 9, f) < 9) { fclose(f); return curry_make_bool(false); }
    uint32_t fw = (uint32_t)(idesc[4] | (idesc[5] << 8));
    uint32_t fh = (uint32_t)(idesc[6] | (idesc[7] << 8));
    int iflags  = idesc[8];
    int has_lct = (iflags >> 7) & 1;
    int lct_size= has_lct ? (2 << (iflags & 7)) : 0;

    uint8_t lct_buf[768];
    memset(lct_buf, 0, sizeof(lct_buf));
    if (has_lct && fread(lct_buf, 3, (size_t)lct_size, f) < (size_t)lct_size) {
        fclose(f); free(lct); return curry_make_bool(false);
    }
    uint8_t *ct = has_lct ? lct_buf : gct;

    /* LZW-compressed image data */
    int lzw_min = fgetc(f);
    if (lzw_min < 2 || lzw_min > 8) { fclose(f); return curry_make_bool(false); }

    /* Collect all sub-blocks into one buffer */
    size_t dlen = 0, dcap = 65536;
    uint8_t *data = malloc(dcap);
    int bsize;
    while ((bsize = fgetc(f)) > 0) {
        if (dlen + (size_t)bsize > dcap) {
            dcap *= 2;
            data = realloc(data, dcap);
        }
        if (fread(data + dlen, 1, (size_t)bsize, f) < (size_t)bsize) break;
        dlen += (size_t)bsize;
    }
    fclose(f);

    /* LZW decode */
    if (fw == 0 || fh == 0 || fh > UINT32_MAX / fw)
        { fclose(f); free(data); return curry_make_bool(false); }
    uint32_t npix = fw * fh;
    uint8_t *indices = calloc(npix, 1);
    if (!indices) { fclose(f); free(data); return curry_make_bool(false); }

    int cc = 1 << lzw_min;
    int eoi = cc + 1;
    int bits_used = lzw_min + 1;

    /* Code table */
    #define GIF_MAX_CODES 4096
    static uint8_t  code_buf[GIF_MAX_CODES][256];
    static uint16_t code_len[GIF_MAX_CODES];
    int next_code = eoi + 1;
    int prev_code = -1;

    /* Initialize with cc+2 singleton codes */
    for (int i = 0; i < cc; i++) { code_buf[i][0] = (uint8_t)i; code_len[i] = 1; }
    code_len[cc] = 0; code_len[eoi] = 0;

    size_t bit_pos = 0;
    uint32_t out_pos = 0;

    /* Read bits from data[] */
    #define GETBITS(n) ({ \
        uint32_t __v = 0; \
        for (int __i = 0; __i < (n); __i++) { \
            size_t __bp = bit_pos + __i; \
            if (__bp/8 < dlen) __v |= ((data[__bp/8] >> (__bp%8)) & 1) << __i; \
        } \
        bit_pos += (n); \
        __v; \
    })

    for (;;) {
        if (bit_pos/8 >= dlen) break;
        int code = (int)GETBITS(bits_used);
        if (code == eoi) break;
        if (code == cc) {
            /* Reset */
            next_code = eoi + 1; bits_used = lzw_min + 1; prev_code = -1; continue;
        }
        if (code >= GIF_MAX_CODES) break;

        uint8_t *seq; int slen;
        if (code < next_code) {
            seq  = code_buf[code];
            slen = code_len[code];
        } else {
            /* code == next_code: KwKwK pattern */
            if (prev_code < 0 || code_len[prev_code] == 0) break;
            seq  = code_buf[prev_code];
            slen = code_len[prev_code];
            /* append first byte of prev code */
        }

        if (next_code < GIF_MAX_CODES && prev_code >= 0) {
            memcpy(code_buf[next_code], code_buf[prev_code], code_len[prev_code]);
            code_buf[next_code][code_len[prev_code]] = seq[0];
            code_len[next_code] = code_len[prev_code] + 1;
            next_code++;
            if (next_code == (1 << bits_used) && bits_used < 12)
                bits_used++;
        }

        if (code >= next_code) {
            /* Append self-ref first byte (KwKwK case) */
            for (int i = 0; i < slen && out_pos < npix; i++)
                indices[out_pos++] = seq[i];
            if (out_pos < npix) indices[out_pos-1] = seq[0]; /* fixup */
        } else {
            for (int i = 0; i < slen && out_pos < npix; i++)
                indices[out_pos++] = seq[i];
        }
        prev_code = code;
    }
    free(data);
    #undef GETBITS

    /* Map palette indices to RGB */
    curry_val img = make_image(w, h, 3);
    curry_val bv  = img_bv(img);
    for (uint32_t i = 0; i < npix && i < w * h; i++) {
        uint8_t idx = indices[i];
        curry_bytevector_set(bv, i*3+0, ct[idx*3+0]);
        curry_bytevector_set(bv, i*3+1, ct[idx*3+1]);
        curry_bytevector_set(bv, i*3+2, ct[idx*3+2]);
    }
    free(indices);
    (void)fh; (void)lct;
    return img;
}

/* ---- Scheme-level procedures ---- */

static curry_val fn_load(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *path = curry_string(av[0]);
    switch (detect_format(path)) {
    case FMT_PNG:  return load_png(path);
    case FMT_JPEG: return load_jpeg(path);
    case FMT_GIF:  return load_gif(path);
    default:       curry_error("image-load: unknown format: %s", path);
    }
}

static curry_val fn_save(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *path = curry_string(av[0]);
    curry_val img    = av[1];
    check_image(img);
    switch (detect_format(path)) {
    case FMT_PNG:  save_png(path, img); break;
    case FMT_JPEG: save_jpeg(path, img, 85); break;
    default:       curry_error("image-save: unsupported output format: %s", path);
    }
    return curry_void();
}

static curry_val fn_make(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    uint32_t w  = (uint32_t)curry_fixnum(av[0]);
    uint32_t h  = (uint32_t)curry_fixnum(av[1]);
    uint32_t ch = (uint32_t)curry_fixnum(av[2]);
    if (ch != 3 && ch != 4) curry_error("image-make: channels must be 3 or 4");
    return make_image(w, h, ch);
}

static curry_val fn_width(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; check_image(av[0]);
    return curry_make_fixnum((intptr_t)img_w(av[0]));
}
static curry_val fn_height(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; check_image(av[0]);
    return curry_make_fixnum((intptr_t)img_h(av[0]));
}
static curry_val fn_channels(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; check_image(av[0]);
    return curry_make_fixnum((intptr_t)img_ch(av[0]));
}
static curry_val fn_pixels(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; check_image(av[0]);
    return img_bv(av[0]);
}

static curry_val fn_ref(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t x = (uint32_t)curry_fixnum(av[1]);
    uint32_t y = (uint32_t)curry_fixnum(av[2]);
    uint32_t c = (uint32_t)curry_fixnum(av[3]);
    return curry_make_fixnum((intptr_t)px_get(av[0], x, y, c));
}

static curry_val fn_set_px(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t x = (uint32_t)curry_fixnum(av[1]);
    uint32_t y = (uint32_t)curry_fixnum(av[2]);
    uint32_t c = (uint32_t)curry_fixnum(av[3]);
    uint8_t  v = (uint8_t)curry_fixnum(av[4]);
    px_set(av[0], x, y, c, v);
    return curry_void();
}

static curry_val fn_crop(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t sx = (uint32_t)curry_fixnum(av[1]);
    uint32_t sy = (uint32_t)curry_fixnum(av[2]);
    uint32_t nw = (uint32_t)curry_fixnum(av[3]);
    uint32_t nh = (uint32_t)curry_fixnum(av[4]);
    uint32_t ch = img_ch(av[0]);
    curry_val out = make_image(nw, nh, ch);
    for (uint32_t y = 0; y < nh; y++)
        for (uint32_t x = 0; x < nw; x++)
            for (uint32_t c = 0; c < ch; c++)
                px_set(out, x, y, c, px_get(av[0], sx+x, sy+y, c));
    return out;
}

static curry_val fn_scale(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t nw = (uint32_t)curry_fixnum(av[1]);
    uint32_t nh = (uint32_t)curry_fixnum(av[2]);
    uint32_t sw = img_w(av[0]), sh = img_h(av[0]), ch = img_ch(av[0]);
    curry_val out = make_image(nw, nh, ch);

    for (uint32_t y = 0; y < nh; y++) {
        for (uint32_t x = 0; x < nw; x++) {
            /* Bilinear sampling */
            double fx = (double)x * (sw - 1) / (double)(nw - 1 > 0 ? nw-1 : 1);
            double fy = (double)y * (sh - 1) / (double)(nh - 1 > 0 ? nh-1 : 1);
            uint32_t x0 = (uint32_t)fx, y0 = (uint32_t)fy;
            uint32_t x1 = x0+1 < sw ? x0+1 : x0;
            uint32_t y1 = y0+1 < sh ? y0+1 : y0;
            double tx = fx - x0, ty = fy - y0;
            for (uint32_t c = 0; c < ch; c++) {
                double v = (1-tx)*(1-ty)*px_get(av[0],x0,y0,c)
                         + tx*(1-ty)*px_get(av[0],x1,y0,c)
                         + (1-tx)*ty*px_get(av[0],x0,y1,c)
                         + tx*ty*px_get(av[0],x1,y1,c);
                px_set(out, x, y, c, (uint8_t)(v + 0.5));
            }
        }
    }
    return out;
}

static curry_val fn_flip_h(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t w = img_w(av[0]), h = img_h(av[0]), ch = img_ch(av[0]);
    curry_val out = make_image(w, h, ch);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            for (uint32_t c = 0; c < ch; c++)
                px_set(out, w-1-x, y, c, px_get(av[0], x, y, c));
    return out;
}

static curry_val fn_flip_v(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t w = img_w(av[0]), h = img_h(av[0]), ch = img_ch(av[0]);
    curry_val out = make_image(w, h, ch);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            for (uint32_t c = 0; c < ch; c++)
                px_set(out, x, h-1-y, c, px_get(av[0], x, y, c));
    return out;
}

static curry_val fn_grayscale(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    check_image(av[0]);
    uint32_t w = img_w(av[0]), h = img_h(av[0]), ch = img_ch(av[0]);
    curry_val out = make_image(w, h, 3);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = px_get(av[0],x,y,0);
            uint8_t g = (ch>1) ? px_get(av[0],x,y,1) : r;
            uint8_t b = (ch>2) ? px_get(av[0],x,y,2) : r;
            uint8_t lum = (uint8_t)(0.299*r + 0.587*g + 0.114*b + 0.5);
            px_set(out,x,y,0,lum); px_set(out,x,y,1,lum); px_set(out,x,y,2,lum);
        }
    return out;
}

static curry_val fn_format(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    switch (detect_format(curry_string(av[0]))) {
    case FMT_PNG:  return curry_make_symbol("png");
    case FMT_JPEG: return curry_make_symbol("jpeg");
    case FMT_GIF:  return curry_make_symbol("gif");
    default:       return curry_make_symbol("unknown");
    }
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "image-load",           fn_load,     1, 1, NULL);
    curry_define_fn(vm, "image-save",           fn_save,     2, 2, NULL);
    curry_define_fn(vm, "image-make",           fn_make,     3, 3, NULL);
    curry_define_fn(vm, "image-width",          fn_width,    1, 1, NULL);
    curry_define_fn(vm, "image-height",         fn_height,   1, 1, NULL);
    curry_define_fn(vm, "image-channels",       fn_channels, 1, 1, NULL);
    curry_define_fn(vm, "image-pixels",         fn_pixels,   1, 1, NULL);
    curry_define_fn(vm, "image-ref",            fn_ref,      4, 4, NULL);
    curry_define_fn(vm, "image-set!",           fn_set_px,   5, 5, NULL);
    curry_define_fn(vm, "image-crop",           fn_crop,     5, 5, NULL);
    curry_define_fn(vm, "image-scale",          fn_scale,    3, 3, NULL);
    curry_define_fn(vm, "image-flip-horizontal",fn_flip_h,   1, 1, NULL);
    curry_define_fn(vm, "image-flip-vertical",  fn_flip_v,   1, 1, NULL);
    curry_define_fn(vm, "image-grayscale",      fn_grayscale,1, 1, NULL);
    curry_define_fn(vm, "image-format",         fn_format,   1, 1, NULL);
}
