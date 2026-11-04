/*
 * modules/qt6/gpu.cpp — gpu-* layer of (curry qt6): GPU rendering and
 * compute through QRhi, Qt's Rendering Hardware Interface.
 *
 * QRhi speaks Metal (macOS), Vulkan (Linux), D3D11/D3D12 (Windows) and
 * OpenGL, so this layer is not tied to OpenGL the way the gl-* layer in
 * qt6.cpp is.  The gl-* layer stays as it is; the two are independent.
 *
 * Design
 * ------
 * Resources (buffers, textures, samplers, shaders, bindings, pipelines) are
 * created from Scheme at any time, with no device yet.  Each one keeps a
 * CPU-side description (and data) and creates its native QRhi object lazily,
 * the first time it is used inside a frame — on whatever QRhi that frame
 * belongs to.  If a resource is later used with a different QRhi, it is
 * re-created there.  When a QRhi is destroyed, a cleanup callback drops
 * every native object made on it.
 *
 * Shaders are Vulkan-style GLSL (#version 440/450, explicit bindings and
 * locations) compiled at creation time by QShaderBaker into SPIR-V, MSL,
 * HLSL and GLSL, so a compile error is raised immediately in Scheme.
 *
 * Frames: a gpu canvas (a QRhiWidget) calls its render procedure with a
 * frame; a headless gpu device runs a frame with gpu-run!.  Inside a frame,
 * gpu-render-pass! / gpu-compute-pass! call a procedure with a pass, and
 * gpu-draw! / gpu-dispatch! RECORD commands into it.  When the procedure
 * returns, every resource the commands reference is materialized (uploads
 * collected into one resource-update batch), then the pass is begun with
 * that batch and the commands are replayed.  Recording first is what lets
 * uploads for lazily-created resources go in before beginPass, where QRhi
 * needs them, and it means a Scheme error inside a pass procedure never
 * leaves the command buffer half-way through a pass.
 *
 * Frame and pass handles carry a serial number, not a pointer, so a handle
 * kept past the end of its frame/pass is detected instead of dangling.
 *
 * Every procedure raises on misuse; no native call is made with unchecked
 * Scheme input.
 */

#include "qt6_common.h"

#include <QApplication>
#include <QWidget>
#include <QRhiWidget>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QOffscreenSurface>
#include <QMatrix4x4>
#include <QFloat16>
#include <QThread>
#include <QCursor>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <rhi/qshaderbaker.h>
/* Same condition qrhi_platform.h uses for QRhiVulkanInitParams. */
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#define CURRY_GPU_VULKAN 1
#include <QVulkanInstance>
#endif

#include <cstring>
#include <cmath>
#include <climits>
#include <vector>
#include <memory>
#include <pthread.h>
#include <array>

/* After every Qt header: object.h defines an `as_bytes` macro that
 * collides with QSpan's as_bytes(). */
extern "C" {
#include "gc.h"             /* gc_register_root, gc_unregister_root */
}
#include "object.h"         /* F64Vec, vis_f64vec, as_f64v */

/* =========================================================================
 * Small helpers
 * ========================================================================= */

static bool sym_is(curry_val v, const char *name) {
    return curry_is_symbol(v) && strcmp(curry_symbol(v), name) == 0;
}

/* Length of a proper list, or -1 for an improper or circular one
 * (tortoise and hare: a circular list used to loop forever here). */
static long list_length(curry_val v) {
    long n = 0;
    curry_val slow = v;
    while (curry_is_pair(v)) {
        n++;
        v = curry_cdr(v);
        if ((n & 1) == 0) {
            slow = curry_cdr(slow);
            if (slow == v && curry_is_pair(v)) return -1;
        }
    }
    return curry_is_nil(v) ? n : -1;
}

/* Keyword-style option list: 'key value 'key value ...  Validates shape
 * and that every key is one of `allowed` (NULL-terminated). */
struct Opts {
    curry_val list;
    const char *who;
    curry_val get(const char *key) const {
        for (curry_val p = list; curry_is_pair(p); p = curry_cdr(curry_cdr(p)))
            if (sym_is(curry_car(p), key)) return curry_car(curry_cdr(p));
        return 0;
    }
    bool has(const char *key) const { return get(key) != 0; }
};

static Opts parse_opts(int ac, curry_val *av, int first, const char *who,
                       const char *const *allowed) {
    if ((ac - first) % 2 != 0)
        curry_error("%s: options must be 'key value pairs", who);
    curry_val lst = curry_nil();
    for (int i = ac - 2; i >= first; i -= 2) {
        if (!curry_is_symbol(av[i]))
            curry_error("%s: argument %d must be an option name symbol", who, i + 1);
        const char *k = curry_symbol(av[i]);
        bool ok = false;
        for (const char *const *a = allowed; *a; a++) if (strcmp(*a, k) == 0) { ok = true; break; }
        if (!ok) curry_error("%s: unknown option '%s", who, k);
        lst = curry_make_pair(av[i], curry_make_pair(av[i + 1], lst));
    }
    return Opts{lst, who};
}

static double opt_real(const Opts &o, const char *key, double dflt) {
    curry_val v = o.get(key);
    if (!v) return dflt;
    if (!curry_is_number(v)) curry_error("%s: option '%s must be a number", o.who, key);
    return curry_number_to_double(v);
}
static long opt_uint(const Opts &o, const char *key, long dflt, long maxv) {
    curry_val v = o.get(key);
    if (!v) return dflt;
    if (!curry_is_fixnum(v) || curry_fixnum(v) < 0 || curry_fixnum(v) > maxv)
        curry_error("%s: option '%s must be an exact integer in [0, %ld]", o.who, key, maxv);
    return (long)curry_fixnum(v);
}
static bool opt_bool(const Opts &o, const char *key, bool dflt) {
    curry_val v = o.get(key);
    if (!v) return dflt;
    return curry_is_true(v);
}
static const char *opt_sym(const Opts &o, const char *key, const char *dflt) {
    curry_val v = o.get(key);
    if (!v) return dflt;
    if (!curry_is_symbol(v)) curry_error("%s: option '%s must be a symbol", o.who, key);
    return curry_symbol(v);
}

/* ---- numeric data -> bytes ---------------------------------------------- */

enum class Elem { F32, U32, I32, F16, UNorm8 };

static void push_elem(QByteArray &out, double x, Elem e) {
    switch (e) {
    case Elem::F32: { float f = (float)x; out.append((const char *)&f, 4); break; }
    case Elem::U32: {
        if (!(x >= 0.0 && x <= 4294967295.0)) curry_error("gpu: value %g out of range for u32", x);
        uint32_t u = (uint32_t)x; out.append((const char *)&u, 4); break; }
    case Elem::I32: {
        if (!(x >= -2147483648.0 && x <= 2147483647.0)) curry_error("gpu: value %g out of range for i32", x);
        int32_t i = (int32_t)x; out.append((const char *)&i, 4); break; }
    case Elem::F16: { qfloat16 h((float)x); out.append((const char *)&h, 2); break; }
    case Elem::UNorm8: {
        double c = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
        out.append((char)(uint8_t)std::lround(c * 255.0)); break; }
    }
}

/* Flatten numbers, (nested) lists, vectors and f64vectors into `out`.
 * A bytevector at the top level is taken as raw bytes. */
/* Upper bound on numbers visited in one conversion.  Depth alone does not
 * bound the work: a vector holding itself 1000 times is only one level
 * of nesting per step but 1000^depth elements. */
static const long k_max_data_elems = 1L << 28;

static void flatten_numbers(QByteArray &out, curry_val v, Elem e, const char *who, int depth = 0,
                            long *budget = nullptr) {
    long local_budget = k_max_data_elems;
    if (!budget) budget = &local_budget;
    if (depth > 64) curry_error("%s: data nested too deeply", who);
    if (--*budget < 0) curry_error("%s: data has more than %ld elements", who, k_max_data_elems);
    if (curry_is_fixnum(v) || curry_is_float(v) || curry_is_number(v)) {
        if (!curry_is_number(v)) curry_error("%s: data element is not a number", who);
        push_elem(out, curry_number_to_double(v), e);
    } else if (vis_f64vec(v)) {
        F64Vec *fv = as_f64v(v);
        if ((*budget -= fv->len) < 0) curry_error("%s: data has more than %ld elements", who, k_max_data_elems);
        for (uint32_t i = 0; i < fv->len; i++) push_elem(out, fv->data[i], e);
    } else if (curry_is_vector(v)) {
        uint32_t n = curry_vector_length(v);
        for (uint32_t i = 0; i < n; i++) flatten_numbers(out, curry_vector_ref(v, i), e, who, depth + 1, budget);
    } else if (curry_is_pair(v) || curry_is_nil(v)) {
        if (list_length(v) < 0) curry_error("%s: data is an improper or circular list", who);
        for (curry_val p = v; curry_is_pair(p); p = curry_cdr(p))
            flatten_numbers(out, curry_car(p), e, who, depth + 1, budget);
    } else if (curry_is_bytevector(v) && depth > 0) {
        curry_error("%s: a bytevector can only be given as the whole data, not nested", who);
    } else {
        curry_error("%s: data must be a bytevector, f64vector, vector or list of real numbers", who);
    }
}

static QByteArray data_bytes(curry_val v, Elem e, const char *who) {
    QByteArray out;
    if (curry_is_bytevector(v)) {
        out = QByteArray((const char *)curry_bytevector_data(v), (qsizetype)curry_bytevector_length(v));
    } else {
        flatten_numbers(out, v, e, who);
    }
    return out;
}

static curry_val bytes_to_bv(const QByteArray &ba) {
    if ((quint64)ba.size() > 0xffffffffull) curry_error("gpu: readback too large");
    uint32_t n = (uint32_t)ba.size();
    curry_val bv = curry_make_bytevector(n, 0);
    if (n) memcpy((void *)curry_bytevector_data(bv), ba.constData(), n);
    return bv;
}

/* =========================================================================
 * Frame / pass state
 * ========================================================================= */

struct GpuBuffer;
struct GpuTexture;
struct GpuSampler;
struct GpuBindings;
struct GpuPipeline;
struct GpuCompute;
struct GpuDevice;

struct DrawCmd {
    GpuPipeline *pl;
    GpuBindings *b;
    std::vector<std::pair<GpuBuffer *, quint32>> vbufs;
    GpuBuffer *ibuf;
    quint32 ioff;
    quint32 count, instances, first, first_instance;
    qint32  vertex_offset;
    bool    has_vp;  float vp[4];
    bool    has_sc;  int   sc[4];
};

struct DispatchCmd {
    GpuCompute  *pl;
    GpuBindings *b;
    int x, y, z;
};

struct PassRec {
    long serial;
    bool compute;
    std::vector<DrawCmd>     draws;
    std::vector<DispatchCmd> dispatches;
};

/* A readback whose result arrives once the frame has completed. */
struct Readback {
    QRhiReadbackResult result;
    curry_val         *proc_slot;   /* GC-rooted; nullptr for synchronous reads */
    bool               done;
};

struct FrameRec {
    long               serial;
    QRhi              *rhi;
    QRhiCommandBuffer *cb;
    QRhiRenderTarget  *screen_rt;     /* canvas target; nullptr for device frames */
    bool               device_frame;
    bool               screen_drawn;  /* a pass targeted the canvas this frame */
    PassRec           *pass;          /* pass currently being recorded, or nullptr */
    std::vector<Readback *> device_readbacks;  /* device frames: run procs after end */
};

static FrameRec *s_frame = nullptr;
static long      s_serial = 0;

static curry_val serial_handle(const char *tag, long serial) {
    return curry_make_pair(curry_make_symbol(tag), curry_make_fixnum(serial));
}
static long handle_serial(const char *tag, curry_val v) {
    if (!curry_is_pair(v) || !sym_is(curry_car(v), tag) || !curry_is_fixnum(curry_cdr(v)))
        curry_error("gpu: expected a %s handle", tag);
    return (long)curry_fixnum(curry_cdr(v));
}

static FrameRec *val_to_frame(curry_val v, const char *who) {
    long s = handle_serial("gpu-frame", v);
    if (!s_frame || s_frame->serial != s)
        curry_error("%s: this frame has already ended (frames are only valid inside their callback)", who);
    return s_frame;
}

static PassRec *val_to_pass(curry_val v, const char *who, bool compute) {
    long s = handle_serial("gpu-pass", v);
    if (!s_frame || !s_frame->pass || s_frame->pass->serial != s)
        curry_error("%s: this pass has already ended (passes are only valid inside their procedure)", who);
    if (s_frame->pass->compute != compute)
        curry_error("%s: needs a %s pass", who, compute ? "compute" : "render");
    return s_frame->pass;
}

static void forbid_during_frame(const char *who) {
    if (s_frame) curry_error("%s: cannot be called while a frame is being recorded", who);
}

/* =========================================================================
 * Resource base: one native incarnation per QRhi at a time.
 * ========================================================================= */

struct GpuRes {
    QRhi *rhi = nullptr;
    bool  released = false;
    unsigned gen = 0;    /* bumped each time the native object is (re)created */
    virtual ~GpuRes() {}
    virtual void drop_natives() = 0;   /* delete native objects; keep CPU state */

    /* Forget the current QRhi: delete natives and remove the cleanup hook. */
    void detach() {
        if (!rhi) return;
        rhi->removeCleanupCallback(this);
        drop_natives();
        rhi = nullptr;
    }
    /* Make `r` the QRhi this resource lives on.  Returns true when natives
     * must be (re)created. */
    bool bind_rhi(QRhi *r) {
        if (rhi == r) return false;
        detach();
        rhi = r;
        r->addCleanupCallback(this, [this](QRhi *) { drop_natives(); rhi = nullptr; });
        return true;
    }
    void check_live(const char *who) const {
        if (released) curry_error("%s: resource has been released", who);
    }
};

template <typename T>
static void delete_native(T *&p) { delete p; p = nullptr; }

static bool is_integer_format(QRhiTexture::Format f);

/* ---- buffers ------------------------------------------------------------ */

struct GpuBuffer : GpuRes {
    QRhiBuffer::Type        type;
    QRhiBuffer::UsageFlags  usage;
    QByteArray              data;      /* CPU copy; size fixed at creation */
    bool                    index_only;
    bool                    dirty = true;
    quint32                 dirty_lo = 0, dirty_hi = 0;
    QRhiBuffer             *buf = nullptr;
    qint64                  max_index = -1;     /* index buffers: largest index, -1 = recompute */
    long                    used_in_frame = 0;  /* uniform buffers: frame serial of last use */
    void drop_natives() override { delete_native(buf); dirty = true; dirty_lo = 0; dirty_hi = (quint32)data.size(); }
    qint64 largest_index() {
        if (max_index < 0) {
            max_index = 0;
            const uchar *p = (const uchar *)data.constData();
            for (qsizetype i = 0; i + 4 <= data.size(); i += 4) {
                quint32 v; memcpy(&v, p + i, 4);
                if ((qint64)v > max_index) max_index = v;
            }
        }
        return max_index;
    }
};

#define val_to_buffer(v) ((GpuBuffer *)val_to_ptr("gpu-buffer", (v)))

/* ---- textures ----------------------------------------------------------- */

struct TexFormatInfo { const char *name; QRhiTexture::Format fmt; int bpp; Elem elem; int comps; };
static const TexFormatInfo k_tex_formats[] = {
    { "rgba8",   QRhiTexture::RGBA8,   4,  Elem::UNorm8, 4 },
    { "r8",      QRhiTexture::R8,      1,  Elem::UNorm8, 1 },
    { "rg8",     QRhiTexture::RG8,     2,  Elem::UNorm8, 2 },
    { "r16f",    QRhiTexture::R16F,    2,  Elem::F16,    1 },
    { "rgba16f", QRhiTexture::RGBA16F, 8,  Elem::F16,    4 },
    { "r32f",    QRhiTexture::R32F,    4,  Elem::F32,    1 },
    { "rgba32f", QRhiTexture::RGBA32F, 16, Elem::F32,    4 },
    { "r32ui",   QRhiTexture::R32UI,   4,  Elem::U32,    1 },
    { "r32si",   QRhiTexture::R32SI,   4,  Elem::I32,    1 },
    { "rgba32ui",QRhiTexture::RGBA32UI,16, Elem::U32,    4 },
    { "rgba32si",QRhiTexture::RGBA32SI,16, Elem::I32,    4 },
};

struct GpuTexture : GpuRes {
    const TexFormatInfo *fi;
    int  w, h, d;                 /* d == 0: 2D texture */
    bool render_target, storage, mipmaps;
    QByteArray data;              /* empty: no initial contents */
    bool dirty = false;
    QRhiTexture             *tex = nullptr;
    QRhiTextureRenderTarget *rt = nullptr;
    QRhiRenderPassDescriptor *rp = nullptr;
    QRhiRenderBuffer        *ds = nullptr;
    void drop_natives() override {
        delete_native(rt); delete_native(rp); delete_native(ds); delete_native(tex);
        dirty = !data.isEmpty();
    }
    qsizetype byte_size() const { return (qsizetype)w * h * (d ? d : 1) * fi->bpp; }
};

#define val_to_texture(v) ((GpuTexture *)val_to_ptr("gpu-texture", (v)))

/* ---- samplers ----------------------------------------------------------- */

struct GpuSampler : GpuRes {
    QRhiSampler::Filter      filter, mip;
    QRhiSampler::AddressMode wrap;
    QRhiSampler *s = nullptr;
    void drop_natives() override { delete_native(s); }
};

#define val_to_sampler(v) ((GpuSampler *)val_to_ptr("gpu-sampler", (v)))

/* ---- shaders (no native object: a baked QShader is backend-neutral) ----- */

struct GpuShader {
    QShader::Stage stage;
    QShader        shader;
};

#define val_to_shader(v) ((GpuShader *)val_to_ptr("gpu-shader", (v)))

/* ---- bindings ----------------------------------------------------------- */

enum class BindKind { Uniform, Texture, StorageRead, StorageWrite, Storage, ImageRead, ImageWrite, Image };

struct BindEntry {
    BindKind kind;
    int binding;
    QRhiShaderResourceBinding::StageFlags stages;
    GpuBuffer  *buf;
    GpuTexture *tex;
    GpuSampler *smp;
    int level;
};

struct GpuBindings : GpuRes {
    std::vector<BindEntry> entries;
    std::vector<unsigned> seen_gens;   /* per entry: buffer, texture, sampler generation */
    unsigned srb_gen = 0;              /* bumped each time srb is rebuilt */
    QRhiShaderResourceBindings *srb = nullptr;
    void drop_natives() override { delete_native(srb); }
};

#define val_to_bindings(v) ((GpuBindings *)val_to_ptr("gpu-bindings", (v)))

/* ---- graphics pipelines ------------------------------------------------- */

struct VInputAttr { int binding, location; QRhiVertexInputAttribute::Format fmt; quint32 offset; };
struct VInputBinding { quint32 stride; bool per_instance; quint32 attr_end; /* bytes of a vertex actually read */ };

struct PipelineVariant {
    QRhiRenderPassDescriptor *rp;     /* owned compatible copy */
    int                       samples;
    QRhiGraphicsPipeline     *ps;
};

struct GpuPipeline : GpuRes {
    GpuShader   *vs, *fs;
    GpuBindings *layout;
    std::vector<VInputBinding> vbindings;
    std::vector<VInputAttr>    vattrs;
    QRhiGraphicsPipeline::Topology topology;
    QRhiGraphicsPipeline::CullMode cull;
    QRhiGraphicsPipeline::FrontFace front;
    QRhiGraphicsPipeline::CompareOp depth_op;
    QRhiGraphicsPipeline::PolygonMode polygon;
    bool  depth_test, depth_write;
    int   blend;                   /* 0 none, 1 alpha, 2 additive */
    float line_width;
    std::vector<PipelineVariant> variants;
    /* The layout srb the variants were created against.  If the bindings
     * object re-creates its srb (a resource moved to another QRhi and
     * back), the variants hold a stale pointer and must be rebuilt. */
    unsigned layout_gen = ~0u;   /* layout->srb_gen the variants were made with */
    void drop_natives() override {
        for (auto &v : variants) { delete v.ps; delete v.rp; }
        variants.clear();
    }
};

#define val_to_pipeline(v) ((GpuPipeline *)val_to_ptr("gpu-pipeline", (v)))

struct GpuCompute : GpuRes {
    GpuShader   *cs;
    GpuBindings *layout;
    QRhiComputePipeline *ps = nullptr;
    unsigned layout_gen = ~0u;   /* see GpuPipeline */
    void drop_natives() override { delete_native(ps); }
};

#define val_to_compute(v) ((GpuCompute *)val_to_ptr("gpu-compute-pipeline", (v)))

/* =========================================================================
 * Materialization (inside a frame, before beginPass)
 * ========================================================================= */

static void need_feature(QRhi *rhi, QRhi::Feature f, const char *what) {
    if (!rhi->isFeatureSupported(f))
        curry_error("gpu: the %s backend does not support %s", rhi->backendName(), what);
}

/* Resources whose upload went into the batch being prepared.  If preparing
 * the pass fails, the batch is released without being submitted, so these
 * are marked dirty again and upload on the next attempt. */
struct BatchUpload { GpuRes *res; quint32 lo, hi; };
static std::vector<BatchUpload> s_batch_uploads;

static void batch_begin() { s_batch_uploads.clear(); }
static void batch_failed();

static void mat_buffer(QRhi *rhi, GpuBuffer *b, QRhiResourceUpdateBatch *u) {
    b->check_live("gpu buffer");
    if (b->bind_rhi(rhi) || !b->buf) {
        if (b->usage & QRhiBuffer::StorageBuffer) need_feature(rhi, QRhi::Compute, "storage buffers");
        b->buf = rhi->newBuffer(b->type, b->usage, (quint32)b->data.size());
        b->gen++;
        if (!b->buf->create()) { delete_native(b->buf); curry_error("gpu: buffer creation failed"); }
        b->dirty = true; b->dirty_lo = 0; b->dirty_hi = (quint32)b->data.size();
    }
    if (b->dirty) {
        quint32 lo = b->dirty_lo, n = b->dirty_hi - b->dirty_lo;
        if (b->type == QRhiBuffer::Dynamic)
            u->updateDynamicBuffer(b->buf, lo, n, b->data.constData() + lo);
        else
            u->uploadStaticBuffer(b->buf, lo, n, b->data.constData() + lo);
        b->dirty = false;
        s_batch_uploads.push_back({ b, lo, lo + n });
    }
    if (b->type == QRhiBuffer::Dynamic && s_frame) b->used_in_frame = s_frame->serial;
}

static void mat_texture(QRhi *rhi, GpuTexture *t, QRhiResourceUpdateBatch *u) {
    t->check_live("gpu texture");
    if (t->bind_rhi(rhi) || !t->tex) {
        QRhiTexture::Flags fl = QRhiTexture::UsedAsTransferSource;
        if (t->render_target) fl |= QRhiTexture::RenderTarget;
        if (t->storage) { need_feature(rhi, QRhi::Compute, "storage images"); fl |= QRhiTexture::UsedWithLoadStore; }
        if (t->mipmaps) fl |= QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
        int maxsz = rhi->resourceLimit(QRhi::TextureSizeMax);
        if (t->w > maxsz || t->h > maxsz)
            curry_error("gpu: a %dx%d texture is over this device's limit of %d", t->w, t->h, maxsz);
        if (t->d) {
            need_feature(rhi, QRhi::ThreeDimensionalTextures, "3D textures");
            fl |= QRhiTexture::ThreeDimensional;
            t->tex = rhi->newTexture(t->fi->fmt, t->w, t->h, t->d, 1, fl);
        } else {
            t->tex = rhi->newTexture(t->fi->fmt, QSize(t->w, t->h), 1, fl);
        }
        t->gen++;
        if (!rhi->isTextureFormatSupported(t->fi->fmt, fl)) {
            delete_native(t->tex);
            curry_error("gpu: texture format %s is not supported with these flags on %s",
                        t->fi->name, rhi->backendName());
        }
        if (!t->tex->create()) { delete_native(t->tex); curry_error("gpu: texture creation failed"); }
        t->dirty = !t->data.isEmpty();
    }
    if (t->dirty) {
        qsizetype slice = (qsizetype)t->w * t->h * t->fi->bpp;
        int layers = t->d ? t->d : 1;
        QList<QRhiTextureUploadEntry> entries;
        for (int z = 0; z < layers; z++) {
            QRhiTextureSubresourceUploadDescription sd(t->data.constData() + z * slice, (quint32)slice);
            entries.append(QRhiTextureUploadEntry(z, 0, sd));
        }
        QRhiTextureUploadDescription desc;
        desc.setEntries(entries.cbegin(), entries.cend());
        u->uploadTexture(t->tex, desc);
        if (t->mipmaps) u->generateMips(t->tex);
        t->dirty = false;
        s_batch_uploads.push_back({ t, 0, 0 });
    }
}

static void batch_failed() {
    /* Restore exactly the range that was pending: re-uploading the whole
     * CPU copy would overwrite results a shader wrote to the rest. */
    for (const BatchUpload &u : s_batch_uploads) {
        if (auto *b = dynamic_cast<GpuBuffer *>(u.res)) {
            if (b->dirty) { b->dirty_lo = std::min(b->dirty_lo, u.lo); b->dirty_hi = std::max(b->dirty_hi, u.hi); }
            else          { b->dirty_lo = u.lo; b->dirty_hi = u.hi; b->dirty = true; }
        } else if (auto *t = dynamic_cast<GpuTexture *>(u.res)) {
            t->dirty = !t->data.isEmpty();
        }
    }
    s_batch_uploads.clear();
}

static void mat_sampler(QRhi *rhi, GpuSampler *s) {
    s->check_live("gpu sampler");
    if (s->bind_rhi(rhi) || !s->s) {
        s->s = rhi->newSampler(s->filter, s->filter, s->mip, s->wrap, s->wrap, s->wrap);
        s->gen++;
        if (!s->s->create()) { delete_native(s->s); curry_error("gpu: sampler creation failed"); }
    }
}

static QRhiShaderResourceBinding make_srb_entry(const BindEntry &e) {
    using B = QRhiShaderResourceBinding;
    switch (e.kind) {
    case BindKind::Uniform:      return B::uniformBuffer(e.binding, e.stages, e.buf->buf);
    case BindKind::Texture:      return B::sampledTexture(e.binding, e.stages, e.tex->tex, e.smp->s);
    case BindKind::StorageRead:  return B::bufferLoad(e.binding, e.stages, e.buf->buf);
    case BindKind::StorageWrite: return B::bufferStore(e.binding, e.stages, e.buf->buf);
    case BindKind::Storage:      return B::bufferLoadStore(e.binding, e.stages, e.buf->buf);
    case BindKind::ImageRead:    return B::imageLoad(e.binding, e.stages, e.tex->tex, e.level);
    case BindKind::ImageWrite:   return B::imageStore(e.binding, e.stages, e.tex->tex, e.level);
    case BindKind::Image:        return B::imageLoadStore(e.binding, e.stages, e.tex->tex, e.level);
    }
    return B();
}

/* Resources referenced by a bindings object are materialized first, so a
 * re-created native resource (new QRhi) always comes with a fresh srb. */
static void mat_bindings(QRhi *rhi, GpuBindings *b, QRhiResourceUpdateBatch *u) {
    b->check_live("gpu bindings");
    /* Compare generations, not native pointers: a re-created native can
     * land at the address of the one just freed (review of #347). */
    bool fresh = b->seen_gens.size() != b->entries.size() * 3;
    b->seen_gens.resize(b->entries.size() * 3, ~0u);
    for (size_t i = 0; i < b->entries.size(); i++) {
        auto &e = b->entries[i];
        if (e.buf) { mat_buffer(rhi, e.buf, u);  fresh |= b->seen_gens[3*i]   != e.buf->gen; b->seen_gens[3*i]   = e.buf->gen; }
        if (e.tex) { mat_texture(rhi, e.tex, u); fresh |= b->seen_gens[3*i+1] != e.tex->gen; b->seen_gens[3*i+1] = e.tex->gen; }
        if (e.smp) { mat_sampler(rhi, e.smp);    fresh |= b->seen_gens[3*i+2] != e.smp->gen; b->seen_gens[3*i+2] = e.smp->gen; }
    }
    if (b->bind_rhi(rhi) || !b->srb || fresh) {
        delete_native(b->srb);
        b->srb = rhi->newShaderResourceBindings();
        b->srb_gen++;
        QList<QRhiShaderResourceBinding> list;
        for (auto &e : b->entries) list.append(make_srb_entry(e));
        b->srb->setBindings(list.cbegin(), list.cend());
        if (!b->srb->create()) { delete_native(b->srb); curry_error("gpu: shader resource bindings creation failed"); }
    }
}

static QRhiGraphicsPipeline *mat_pipeline(QRhi *rhi, GpuPipeline *p, QRhiRenderTarget *rt,
                                          QRhiResourceUpdateBatch *u) {
    p->check_live("gpu pipeline");
    mat_bindings(rhi, p->layout, u);
    p->bind_rhi(rhi);
    if (p->layout_gen != p->layout->srb_gen) { p->drop_natives(); p->layout_gen = p->layout->srb_gen; }
    QRhiRenderPassDescriptor *rpd = rt->renderPassDescriptor();
    int samples = rt->sampleCount();
    for (auto &v : p->variants)
        if (v.samples == samples && v.rp->isCompatible(rpd)) return v.ps;

    QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
    ps->setShaderStages({ { QRhiShaderStage::Vertex, p->vs->shader },
                          { QRhiShaderStage::Fragment, p->fs->shader } });
    QRhiVertexInputLayout il;
    QList<QRhiVertexInputBinding> vb;
    for (auto &b : p->vbindings)
        vb.append(QRhiVertexInputBinding(b.stride, b.per_instance ? QRhiVertexInputBinding::PerInstance
                                                                   : QRhiVertexInputBinding::PerVertex));
    QList<QRhiVertexInputAttribute> va;
    for (auto &a : p->vattrs) va.append(QRhiVertexInputAttribute(a.binding, a.location, a.fmt, a.offset));
    il.setBindings(vb.cbegin(), vb.cend());
    il.setAttributes(va.cbegin(), va.cend());
    ps->setVertexInputLayout(il);
    ps->setShaderResourceBindings(p->layout->srb);
    ps->setFlags(QRhiGraphicsPipeline::UsesScissor);   /* every draw sets one; see execute_render_pass */
    ps->setTopology(p->topology);
    ps->setCullMode(p->cull);
    ps->setFrontFace(p->front);
    ps->setDepthTest(p->depth_test);
    ps->setDepthWrite(p->depth_write);
    ps->setDepthOp(p->depth_op);
    ps->setSampleCount(samples);
    if (p->line_width != 1.0f) { need_feature(rhi, QRhi::WideLines, "wide lines"); ps->setLineWidth(p->line_width); }
    if (p->polygon != QRhiGraphicsPipeline::Fill) {
        need_feature(rhi, QRhi::NonFillPolygonMode, "line polygon mode");
        ps->setPolygonMode(p->polygon);
    }
    if (p->blend) {
        QRhiGraphicsPipeline::TargetBlend tb;
        tb.enable = true;
        tb.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        tb.dstColor = p->blend == 1 ? QRhiGraphicsPipeline::OneMinusSrcAlpha : QRhiGraphicsPipeline::One;
        tb.srcAlpha = QRhiGraphicsPipeline::One;
        tb.dstAlpha = p->blend == 1 ? QRhiGraphicsPipeline::OneMinusSrcAlpha : QRhiGraphicsPipeline::One;
        ps->setTargetBlends({ tb });
    }
    QRhiRenderPassDescriptor *own = rpd->newCompatibleRenderPassDescriptor();
    ps->setRenderPassDescriptor(own);
    if (!ps->create()) {
        delete ps; delete own;
        curry_error("gpu: graphics pipeline creation failed on %s (see Qt's warning output)", rhi->backendName());
    }
    p->variants.push_back({ own, samples, ps });
    return ps;
}

static QRhiComputePipeline *mat_compute(QRhi *rhi, GpuCompute *c, QRhiResourceUpdateBatch *u) {
    c->check_live("gpu compute pipeline");
    need_feature(rhi, QRhi::Compute, "compute shaders");
    mat_bindings(rhi, c->layout, u);
    if (c->layout_gen != c->layout->srb_gen) { delete_native(c->ps); c->layout_gen = c->layout->srb_gen; }
    if (c->bind_rhi(rhi) || !c->ps) {
        c->ps = rhi->newComputePipeline();
        c->ps->setShaderStage({ QRhiShaderStage::Compute, c->cs->shader });
        c->ps->setShaderResourceBindings(c->layout->srb);
        if (!c->ps->create()) {
            delete_native(c->ps);
            curry_error("gpu: compute pipeline creation failed on %s (see Qt's warning output)", rhi->backendName());
        }
    }
    return c->ps;
}

static QRhiTextureRenderTarget *mat_texture_target(QRhi *rhi, GpuTexture *t, QRhiResourceUpdateBatch *u) {
    if (!t->render_target) curry_error("gpu-render-pass!: target texture was not created with 'render-target #t");
    if (t->d) curry_error("gpu-render-pass!: a 3D texture cannot be a render target");
    mat_texture(rhi, t, u);
    if (!t->rt) {
        t->ds = rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(t->w, t->h), 1);
        if (!t->ds->create()) { delete_native(t->ds); curry_error("gpu: depth buffer creation failed"); }
        QRhiTextureRenderTargetDescription desc{ QRhiColorAttachment(t->tex) };
        desc.setDepthStencilBuffer(t->ds);
        t->rt = rhi->newTextureRenderTarget(desc);
        t->rp = t->rt->newCompatibleRenderPassDescriptor();
        t->rt->setRenderPassDescriptor(t->rp);
        if (!t->rt->create()) { t->detach(); curry_error("gpu: render target creation failed"); }
    }
    return t->rt;
}

/* =========================================================================
 * Devices (headless QRhi)
 * ========================================================================= */

struct GpuDevice {
    QRhi *rhi;
    QOffscreenSurface *fallback;   /* OpenGL only */
};

#define val_to_device(v) ((GpuDevice *)val_to_ptr("gpu-device", (v)))

#ifdef CURRY_GPU_VULKAN
static QVulkanInstance *s_vk_inst = nullptr;
static QVulkanInstance *vulkan_instance() {
    if (!s_vk_inst) {
        s_vk_inst = new QVulkanInstance();
        s_vk_inst->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!s_vk_inst->create()) { delete s_vk_inst; s_vk_inst = nullptr; }
    }
    return s_vk_inst;
}
#endif

static QRhi *create_rhi(QRhi::Implementation impl, QOffscreenSurface **fallback) {
    *fallback = nullptr;
    switch (impl) {
    case QRhi::Null: { QRhiNullInitParams p; return QRhi::create(QRhi::Null, &p); }
#if QT_CONFIG(opengl)
    case QRhi::OpenGLES2: {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
#if !defined(Q_OS_MACOS)
        fmt.setVersion(4, 3);   /* compute shaders; macOS stops at 4.1 regardless */
#endif
        QRhiGles2InitParams p;
        p.format = fmt;
        QOffscreenSurface *surf = QRhiGles2InitParams::newFallbackSurface(fmt);
        p.fallbackSurface = surf;
        QRhi *r = QRhi::create(QRhi::OpenGLES2, &p);
        if (r) *fallback = surf; else delete surf;
        return r;
    }
#endif
#ifdef CURRY_GPU_VULKAN
    case QRhi::Vulkan: {
        QVulkanInstance *inst = vulkan_instance();
        if (!inst) return nullptr;
        QRhiVulkanInitParams p;
        p.inst = inst;
        return QRhi::create(QRhi::Vulkan, &p);
    }
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    case QRhi::Metal: { QRhiMetalInitParams p; return QRhi::create(QRhi::Metal, &p); }
#endif
#if defined(Q_OS_WIN)
    case QRhi::D3D11: { QRhiD3D11InitParams p; return QRhi::create(QRhi::D3D11, &p); }
    case QRhi::D3D12: { QRhiD3D12InitParams p; return QRhi::create(QRhi::D3D12, &p); }
#endif
    default: return nullptr;
    }
}

struct BackendName { const char *name; QRhi::Implementation impl; QRhiWidget::Api api; };
static const BackendName k_backends[] = {
    { "metal",  QRhi::Metal,     QRhiWidget::Api::Metal },
    { "vulkan", QRhi::Vulkan,    QRhiWidget::Api::Vulkan },
    { "opengl", QRhi::OpenGLES2, QRhiWidget::Api::OpenGL },
    { "d3d11",  QRhi::D3D11,     QRhiWidget::Api::Direct3D11 },
    { "d3d12",  QRhi::D3D12,     QRhiWidget::Api::Direct3D12 },
    { "null",   QRhi::Null,      QRhiWidget::Api::Null },
};

static const BackendName *find_backend(const char *name, const char *who) {
    for (auto &b : k_backends) if (strcmp(b.name, name) == 0) return &b;
    curry_error("%s: unknown backend '%s (expected metal, vulkan, opengl, d3d11, d3d12, null or default)", who, name);
}

static const char *impl_name(QRhi::Implementation impl) {
    for (auto &b : k_backends) if (b.impl == impl) return b.name;
    return "unknown";
}

static curry_val fn_make_gpu_device(int ac, curry_val *av, void *) {
    ensure_app();
    const char *want = ac > 0 ? checked_symbol(av[0], 1, "make-gpu-device") : "default";
    std::vector<QRhi::Implementation> order;
    if (strcmp(want, "default") == 0) {
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
        order = { QRhi::Metal, QRhi::OpenGLES2 };
#elif defined(Q_OS_WIN)
        order = { QRhi::D3D12, QRhi::D3D11, QRhi::Vulkan, QRhi::OpenGLES2 };
#else
        order = { QRhi::Vulkan, QRhi::OpenGLES2 };
#endif
    } else {
        order = { find_backend(want, "make-gpu-device")->impl };
    }
    for (auto impl : order) {
        QOffscreenSurface *fb = nullptr;
        QRhi *r = create_rhi(impl, &fb);
        if (r) return ptr_to_val("gpu-device", new GpuDevice{ r, fb });
    }
    curry_error("make-gpu-device: could not create a %s GPU device", want);
}

static curry_val fn_gpu_device_destroy(int, curry_val *av, void *) {
    forbid_during_frame("gpu-device-destroy!");
    GpuDevice *d = val_to_device(av[0]);
    if (!d->rhi) return curry_void();
    delete d->rhi;            /* runs cleanup callbacks: resources drop their natives */
    d->rhi = nullptr;
    delete d->fallback; d->fallback = nullptr;
    return curry_void();
}

static QRhi *device_rhi(GpuDevice *d, const char *who) {
    if (!d->rhi) curry_error("%s: device has been destroyed", who);
    return d->rhi;
}

/* =========================================================================
 * Canvas (QRhiWidget)
 * ========================================================================= */

struct GpuCanvasState {
    curry_val render_proc, key_proc, key_up_proc, mouse_proc, scroll_proc, resize_proc;
    curry_val grab_move_proc;    /* (lambda (dx dy) ...) while the mouse is grabbed */
    bool      mouse_grabbed;
    QPoint    grab_warp_target;  /* our own cursor warp, to ignore its move event */
};

struct WindFrame;
static WindFrame *push_wind(curry_val after);
static void pop_wind(WindFrame *wf);
static curry_val s_end_canvas_frame_prim = 0;

class CurryGpuCanvas : public QRhiWidget {
public:
    GpuCanvasState st;
    explicit CurryGpuCanvas(QWidget *parent = nullptr) : QRhiWidget(parent) {
        st.render_proc = st.key_proc = st.key_up_proc = st.mouse_proc =
            st.scroll_proc = st.resize_proc = st.grab_move_proc = curry_make_bool(false);
        st.mouse_grabbed = false;
        st.grab_warp_target = QPoint(-1, -1);
        gc_register_root(&st.render_proc);
        gc_register_root(&st.key_proc);
        gc_register_root(&st.key_up_proc);
        gc_register_root(&st.mouse_proc);
        gc_register_root(&st.scroll_proc);
        gc_register_root(&st.resize_proc);
        gc_register_root(&st.grab_move_proc);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_AcceptTouchEvents, false);
    }
    QRhi *canvas_rhi() const { return rhi(); }
protected:
    void initialize(QRhiCommandBuffer *) override {}
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override {}
    void keyPressEvent(QKeyEvent *ev) override { fire_key(st.key_proc, ev); }
    void keyReleaseEvent(QKeyEvent *ev) override {
        if (ev->isAutoRepeat()) return;
        fire_key(st.key_up_proc, ev);
    }
    void mousePressEvent(QMouseEvent *ev) override       { fire_mouse("press", ev); }
    void mouseReleaseEvent(QMouseEvent *ev) override     { fire_mouse("release", ev); }
    void mouseDoubleClickEvent(QMouseEvent *ev) override { fire_mouse("double-press", ev); }
    void mouseMoveEvent(QMouseEvent *ev) override {
        if (st.mouse_grabbed) { grabbed_move(ev); return; }
        fire_mouse(ev->buttons() != Qt::NoButton ? "drag" : "move", ev);
    }
    void wheelEvent(QWheelEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override {
        QRhiWidget::resizeEvent(ev);
        if (curry_is_bool(st.resize_proc)) return;
        curry_val argv[2] = { curry_make_fixnum(width()), curry_make_fixnum(height()) };
        SCHEME_CALL(curry_apply(st.resize_proc, 2, argv));
    }
private:
    /* Mouse capture, as qt6.cpp's canvas does it: report the motion
     * relative to the widget centre, then warp the cursor back there.  The
     * move event our own warp produces is recognised and skipped. */
    void grabbed_move(QMouseEvent *ev) {
        if (curry_is_bool(st.grab_move_proc)) return;
        QPoint global = ev->globalPosition().toPoint();
        if (global == st.grab_warp_target) return;
        QPoint center = mapToGlobal(rect().center());
        int dx = global.x() - center.x(), dy = global.y() - center.y();
        if (dx == 0 && dy == 0) return;
        st.grab_warp_target = center;
        QCursor::setPos(center);
        curry_val argv[2] = { curry_make_fixnum(dx), curry_make_fixnum(dy) };
        SCHEME_CALL(curry_apply(st.grab_move_proc, 2, argv));
    }
public:
    void set_grab(bool on) {
        if (on == st.mouse_grabbed) return;
        st.mouse_grabbed = on;
        if (on) {
            st.grab_warp_target = QPoint(-1, -1);
            grabMouse();
            setCursor(Qt::BlankCursor);
        } else {
            releaseMouse();
            unsetCursor();
        }
    }
private:
    void fire_key(curry_val proc, QKeyEvent *ev) {
        if (curry_is_bool(proc)) return;
        curry_val argv[2] = { curry_make_string(qt_key_str(ev->key(), ev->text())),
                              build_mods(ev->modifiers()) };
        SCHEME_CALL(curry_apply(proc, 2, argv));
    }
    void fire_mouse(const char *etype, QMouseEvent *ev) {
        if (curry_is_bool(st.mouse_proc)) return;
        const char *btn = "none";
        switch (ev->button()) {
        case Qt::LeftButton:   btn = "left";   break;
        case Qt::RightButton:  btn = "right";  break;
        case Qt::MiddleButton: btn = "middle"; break;
        default: break;
        }
        curry_val argv[5] = { curry_make_symbol(etype), curry_make_symbol(btn),
                              curry_make_fixnum((intptr_t)ev->position().x()),
                              curry_make_fixnum((intptr_t)ev->position().y()),
                              build_mods(ev->modifiers()) };
        SCHEME_CALL(curry_apply(st.mouse_proc, 5, argv));
    }
};

void CurryGpuCanvas::wheelEvent(QWheelEvent *ev) {
    if (curry_is_bool(st.scroll_proc)) { ev->ignore(); return; }
    QPoint pd = ev->pixelDelta(), ad = ev->angleDelta();
    double dx = pd.isNull() ? (double)ad.x() / 8.0 : (double)pd.x();
    double dy = pd.isNull() ? (double)ad.y() / 8.0 : (double)pd.y();
    curry_val argv[5] = { curry_make_float(dx), curry_make_float(dy),
                          curry_make_fixnum((intptr_t)ev->position().x()),
                          curry_make_fixnum((intptr_t)ev->position().y()),
                          build_mods(ev->modifiers()) };
    SCHEME_CALL(curry_apply(st.scroll_proc, 5, argv));
    ev->accept();
}

static CurryGpuCanvas *val_to_gpu_canvas(curry_val v, const char *who) {
    QWidget *w = (QWidget *)val_to_ptr("qt6-widget", v);
    auto *c = dynamic_cast<CurryGpuCanvas *>(w);
    if (!c) curry_error("%s: widget is not a gpu canvas", who);
    return c;
}

/* Clear-only pass, used when a render procedure drew nothing to the canvas
 * (QRhiWidget's texture contents are undefined otherwise). */
static void clear_screen(FrameRec *f) {
    f->cb->beginPass(f->screen_rt, QColor(0, 0, 0, 255), { 1.0f, 0 });
    f->cb->endPass();
}


void CurryGpuCanvas::render(QRhiCommandBuffer *cb) {
    if (s_frame) return;   /* re-entrant render (nested event loop) — skip */
    FrameRec f{};
    f.serial = ++s_serial;
    f.rhi = rhi();
    f.cb = cb;
    f.screen_rt = renderTarget();
    f.device_frame = false;
    s_frame = &f;
    if (!curry_is_bool(st.render_proc)) {
        QSize ps = f.screen_rt->pixelSize();
        curry_val argv[3] = { serial_handle("gpu-frame", f.serial),
                              curry_make_fixnum(ps.width()), curry_make_fixnum(ps.height()) };
        /* An escape out of the render procedure must not leave s_frame
         * pointing at this (dead) stack frame. */
        WindFrame *wf = push_wind(s_end_canvas_frame_prim);
        SCHEME_CALL(curry_apply(st.render_proc, 3, argv));
        pop_wind(wf);
    }
    if (!f.screen_drawn) clear_screen(&f);
    s_frame = nullptr;
}

static curry_val fn_make_gpu_canvas(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "backend", "samples", "format", "debug", nullptr };
    ensure_app();
    Opts o = parse_opts(ac, av, 0, "make-gpu-canvas", allowed);
    const char *backend = opt_sym(o, "backend", "default");
    long samples = opt_uint(o, "samples", 1, 64);
    const char *fmt = opt_sym(o, "format", "rgba8");
    QRhiWidget::TextureFormat tf;
    if      (strcmp(fmt, "rgba8") == 0)   tf = QRhiWidget::TextureFormat::RGBA8;
    else if (strcmp(fmt, "rgba16f") == 0) tf = QRhiWidget::TextureFormat::RGBA16F;
    else if (strcmp(fmt, "rgba32f") == 0) tf = QRhiWidget::TextureFormat::RGBA32F;
    else if (strcmp(fmt, "rgb10a2") == 0) tf = QRhiWidget::TextureFormat::RGB10A2;
    else curry_error("make-gpu-canvas: 'format must be rgba8, rgba16f, rgba32f or rgb10a2");
    if (samples != 1 && samples != 2 && samples != 4 && samples != 8)
        curry_error("make-gpu-canvas: 'samples must be 1, 2, 4 or 8");
    const BackendName *be = strcmp(backend, "default") == 0 ? nullptr : find_backend(backend, "make-gpu-canvas");

    auto *c = new CurryGpuCanvas();
    if (be) c->setApi(be->api);
    c->setSampleCount((int)samples);
    c->setColorBufferFormat(tf);
    if (opt_bool(o, "debug", false)) c->setDebugLayerEnabled(true);
    return ptr_to_val("qt6-widget", c);
}

#define CANVAS_SETTER(fname, field, who)                                     \
    static curry_val fname(int, curry_val *av, void *) {                     \
        CurryGpuCanvas *c = val_to_gpu_canvas(av[0], who);                   \
        if (!curry_is_procedure(av[1]) && !curry_is_bool(av[1]))             \
            curry_error("%s: argument 2 must be a procedure or #f", who);    \
        /* gc_register_root keeps a copy of the value seen at          \
         * registration and the canvas is not GC-scanned, so re-root     \
         * the slot after every store (review of #347). */              \
        gc_unregister_root(&c->st.field);                                    \
        c->st.field = av[1];                                                 \
        gc_register_root(&c->st.field);                                      \
        return curry_void();                                                 \
    }
CANVAS_SETTER(fn_gpu_canvas_on_render, render_proc, "gpu-canvas-on-render!")
CANVAS_SETTER(fn_gpu_canvas_on_key,    key_proc,    "gpu-canvas-on-key!")
CANVAS_SETTER(fn_gpu_canvas_on_key_up, key_up_proc, "gpu-canvas-on-key-up!")
CANVAS_SETTER(fn_gpu_canvas_on_mouse,  mouse_proc,  "gpu-canvas-on-mouse!")
CANVAS_SETTER(fn_gpu_canvas_on_scroll, scroll_proc, "gpu-canvas-on-scroll!")
CANVAS_SETTER(fn_gpu_canvas_on_resize, resize_proc, "gpu-canvas-on-resize!")
CANVAS_SETTER(fn_gpu_canvas_on_grab_move, grab_move_proc, "gpu-canvas-on-grab-move!")

static curry_val fn_gpu_canvas_grab_mouse(int, curry_val *av, void *) {
    val_to_gpu_canvas(av[0], "gpu-canvas-grab-mouse!")->set_grab(true);
    return curry_void();
}
static curry_val fn_gpu_canvas_release_mouse(int, curry_val *av, void *) {
    val_to_gpu_canvas(av[0], "gpu-canvas-release-mouse!")->set_grab(false);
    return curry_void();
}
static curry_val fn_gpu_canvas_mouse_grabbed_p(int, curry_val *av, void *) {
    return curry_make_bool(val_to_gpu_canvas(av[0], "gpu-canvas-mouse-grabbed?")->st.mouse_grabbed);
}
/* Keyboard focus to the canvas, so its key callbacks fire without a click
 * first (otherwise a focusable sidebar widget such as a slider can take
 * the arrow keys). */
static curry_val fn_gpu_canvas_focus(int, curry_val *av, void *) {
    val_to_gpu_canvas(av[0], "gpu-canvas-focus!")->setFocus(Qt::OtherFocusReason);
    return curry_void();
}

static curry_val fn_gpu_canvas_redraw(int, curry_val *av, void *) {
    val_to_gpu_canvas(av[0], "gpu-canvas-redraw!")->update();
    return curry_void();
}

static curry_val fn_gpu_canvas_save_png(int, curry_val *av, void *) {
    CurryGpuCanvas *c = val_to_gpu_canvas(av[0], "gpu-canvas-save-png!");
    const char *path = checked_string(av[1], 2, "gpu-canvas-save-png!");
    forbid_during_frame("gpu-canvas-save-png!");
    QImage img = c->grabFramebuffer();
    return curry_make_bool(!img.isNull() && img.save(QString::fromUtf8(path), "PNG"));
}

/* =========================================================================
 * Queries (device, canvas or frame)
 * ========================================================================= */

/* A canvas's QRhi exists only once it has been shown; returns nullptr before. */
static QRhi *any_rhi(curry_val v, const char *who) {
    if (curry_is_pair(v) && sym_is(curry_car(v), "gpu-device")) return device_rhi(val_to_device(v), who);
    if (curry_is_pair(v) && sym_is(curry_car(v), "gpu-frame")) return val_to_frame(v, who)->rhi;
    if (curry_is_pair(v) && sym_is(curry_car(v), "qt6-widget")) return val_to_gpu_canvas(v, who)->canvas_rhi();
    curry_error("%s: expected a gpu device, gpu canvas or frame", who);
}

static curry_val fn_gpu_backend(int, curry_val *av, void *) {
    QRhi *r = any_rhi(av[0], "gpu-backend");
    return r ? curry_make_symbol(impl_name(r->backend())) : curry_make_bool(false);
}

static curry_val fn_gpu_device_name(int, curry_val *av, void *) {
    QRhi *r = any_rhi(av[0], "gpu-device-name");
    if (!r) return curry_make_bool(false);
    return curry_make_string(r->driverInfo().deviceName.constData());
}

struct FeatureName { const char *name; QRhi::Feature f; };
static const FeatureName k_features[] = {
    { "compute",               QRhi::Compute },
    { "instancing",            QRhi::Instancing },
    { "base-instance",         QRhi::BaseInstance },
    { "base-vertex",           QRhi::BaseVertex },
    { "3d-textures",           QRhi::ThreeDimensionalTextures },
    { "texture-arrays",        QRhi::TextureArrays },
    { "geometry-shader",       QRhi::GeometryShader },
    { "tessellation",          QRhi::Tessellation },
    { "multiview",             QRhi::MultiView },
    { "variable-rate-shading", QRhi::VariableRateShading },
    { "draw-indirect",         QRhi::DrawIndirect },
    { "wide-lines",            QRhi::WideLines },
    { "line-polygon-mode",     QRhi::NonFillPolygonMode },
    { "int-attributes",        QRhi::IntAttributes },
    { "read-back-any-format",  QRhi::ReadBackAnyTextureFormat },
    { "depth-clamp",           QRhi::DepthClamp },
    { "timestamps",            QRhi::Timestamps },
};

static curry_val fn_gpu_feature_p(int, curry_val *av, void *) {
    const char *name = checked_symbol(av[1], 2, "gpu-feature?");
    for (auto &f : k_features) {
        if (strcmp(f.name, name) == 0) {
            QRhi *r = any_rhi(av[0], "gpu-feature?");
            if (!r) curry_error("gpu-feature?: canvas has no GPU device yet (show its window first)");
            return curry_make_bool(r->isFeatureSupported(f.f));
        }
    }
    curry_error("gpu-feature?: unknown feature '%s", name);
}

static curry_val fn_gpu_features(int, curry_val *, void *) {
    curry_val l = curry_nil();
    for (int i = (int)(sizeof k_features / sizeof k_features[0]) - 1; i >= 0; i--)
        l = curry_make_pair(curry_make_symbol(k_features[i].name), l);
    return l;
}

/* 16-element column-major vector, like the gl-* mat4 uniform convention. */
static curry_val fn_gpu_clip_space_matrix(int, curry_val *av, void *) {
    QRhi *r = any_rhi(av[0], "gpu-clip-space-matrix");
    if (!r) curry_error("gpu-clip-space-matrix: canvas has no GPU device yet (show its window first)");
    QMatrix4x4 m = r->clipSpaceCorrMatrix();
    curry_val v = curry_make_vector(16, curry_make_float(0.0));
    const float *d = m.constData();   /* column-major */
    for (int i = 0; i < 16; i++) curry_vector_set(v, (uint32_t)i, curry_make_float(d[i]));
    return v;
}

static curry_val fn_gpu_y_up_p(int, curry_val *av, void *) {
    QRhi *r = any_rhi(av[0], "gpu-framebuffer-y-up?");
    if (!r) curry_error("gpu-framebuffer-y-up?: canvas has no GPU device yet (show its window first)");
    return curry_make_bool(r->isYUpInFramebuffer());
}

static curry_val fn_gpu_limit(int, curry_val *av, void *) {
    static const struct { const char *n; QRhi::ResourceLimit l; } k[] = {
        { "texture-size-max", QRhi::TextureSizeMax },
        { "max-color-attachments", QRhi::MaxColorAttachments },
        { "max-thread-groups-per-dimension", QRhi::MaxThreadGroupsPerDimension },
        { "max-threads-per-thread-group", QRhi::MaxThreadsPerThreadGroup },
        { "max-thread-group-x", QRhi::MaxThreadGroupX },
        { "max-thread-group-y", QRhi::MaxThreadGroupY },
        { "max-thread-group-z", QRhi::MaxThreadGroupZ },
        { "max-uniform-buffer-range", QRhi::MaxUniformBufferRange },
        { "max-vertex-inputs", QRhi::MaxVertexInputs },
        { "max-vertex-outputs", QRhi::MaxVertexOutputs },
    };
    const char *name = checked_symbol(av[1], 2, "gpu-limit");
    for (auto &e : k) if (strcmp(e.n, name) == 0) {
        QRhi *r = any_rhi(av[0], "gpu-limit");
        if (!r) curry_error("gpu-limit: canvas has no GPU device yet (show its window first)");
        return curry_make_fixnum(r->resourceLimit(e.l));
    }
    curry_error("gpu-limit: unknown limit '%s", name);
}

/* =========================================================================
 * Resource constructors
 * ========================================================================= */

static QRhiBuffer::UsageFlags parse_buffer_usage(curry_val v, bool *index_only) {
    QRhiBuffer::UsageFlags u;
    curry_val lst = curry_is_symbol(v) ? curry_make_pair(v, curry_nil()) : v;
    if (list_length(lst) <= 0) curry_error("make-gpu-buffer: usage must be a symbol or a non-empty list of symbols");
    int n_index = 0, n = 0;
    for (curry_val p = lst; curry_is_pair(p); p = curry_cdr(p)) {
        curry_val s = curry_car(p);
        n++;
        if      (sym_is(s, "vertex"))  u |= QRhiBuffer::VertexBuffer;
        else if (sym_is(s, "index"))   { u |= QRhiBuffer::IndexBuffer; n_index++; }
        else if (sym_is(s, "uniform")) u |= QRhiBuffer::UniformBuffer;
        else if (sym_is(s, "storage")) u |= QRhiBuffer::StorageBuffer;
        else curry_error("make-gpu-buffer: unknown usage (expected vertex, index, uniform or storage)");
    }
    if ((u & QRhiBuffer::UniformBuffer) && u != QRhiBuffer::UsageFlags(QRhiBuffer::UniformBuffer))
        curry_error("make-gpu-buffer: a uniform buffer cannot have other usages");
    /* gpu-draw! checks every index against the vertex buffers using the
     * CPU copy; a shader writing the indices would make that copy stale. */
    if ((u & QRhiBuffer::IndexBuffer) && (u & QRhiBuffer::StorageBuffer))
        curry_error("make-gpu-buffer: an index buffer cannot also be a storage buffer");
    /* Numbers for a buffer with any index usage are packed as u32: a
     * '(vertex index) buffer used to get float bit patterns as indices. */
    *index_only = n_index > 0;
    (void)n;
    return u;
}

/* (make-gpu-buffer usage data-or-size) */
static curry_val fn_make_gpu_buffer(int, curry_val *av, void *) {
    bool index_only = false;
    QRhiBuffer::UsageFlags usage = parse_buffer_usage(av[0], &index_only);
    QByteArray data;
    if (curry_is_fixnum(av[1])) {
        intptr_t n = curry_fixnum(av[1]);
        if (n <= 0 || n > (intptr_t)1 << 31) curry_error("make-gpu-buffer: size must be in [1, 2^31] bytes");
        data = QByteArray((qsizetype)n, '\0');
    } else {
        data = data_bytes(av[1], index_only ? Elem::U32 : Elem::F32, "make-gpu-buffer");
        if (data.isEmpty()) curry_error("make-gpu-buffer: data is empty");
    }
    if ((usage & QRhiBuffer::UniformBuffer) && (data.size() % 16) != 0)
        data.append(QByteArray(16 - data.size() % 16, '\0'));   /* std140 blocks are 16-byte multiples */
    auto *b = new GpuBuffer();
    b->usage = usage;
    b->type = (usage & QRhiBuffer::UniformBuffer) ? QRhiBuffer::Dynamic
            : (usage & QRhiBuffer::StorageBuffer) ? QRhiBuffer::Static : QRhiBuffer::Static;
    b->index_only = index_only;
    b->data = data;
    b->dirty_hi = (quint32)data.size();
    return ptr_to_val("gpu-buffer", b);
}

/* (gpu-buffer-update! buf data [byte-offset]) */
static curry_val fn_gpu_buffer_update(int ac, curry_val *av, void *) {
    GpuBuffer *b = val_to_buffer(av[0]);
    b->check_live("gpu-buffer-update!");
    QByteArray nd = data_bytes(av[1], b->index_only ? Elem::U32 : Elem::F32, "gpu-buffer-update!");
    intptr_t off = ac > 2 ? checked_fixnum(av[2], 3, "gpu-buffer-update!") : 0;
    if (off < 0 || off > b->data.size() || nd.size() > b->data.size() - off)
        curry_error("gpu-buffer-update!: %lld bytes at offset %lld do not fit in a %lld-byte buffer",
                    (long long)nd.size(), (long long)off, (long long)b->data.size());
    if (nd.isEmpty()) return curry_void();
    if (b->type == QRhiBuffer::Dynamic && s_frame && b->used_in_frame == s_frame->serial)
        curry_error("gpu-buffer-update!: this uniform buffer was already used in this frame; "
                    "a uniform buffer holds one value per frame, so use a second buffer");
    memcpy(b->data.data() + off, nd.constData(), (size_t)nd.size());
    b->max_index = -1;
    quint32 lo = (quint32)off, hi = (quint32)(off + nd.size());
    if (b->dirty) { b->dirty_lo = std::min(b->dirty_lo, lo); b->dirty_hi = std::max(b->dirty_hi, hi); }
    else          { b->dirty_lo = lo; b->dirty_hi = hi; b->dirty = true; }
    return curry_void();
}

static curry_val fn_gpu_buffer_size(int, curry_val *av, void *) {
    return curry_make_fixnum((intptr_t)val_to_buffer(av[0])->data.size());
}

static const TexFormatInfo *find_tex_format(const char *name, const char *who) {
    for (auto &f : k_tex_formats) if (strcmp(f.name, name) == 0) return &f;
    curry_error("%s: unknown texture format '%s", who, name);
}

static QByteArray texture_data(const GpuTexture *t, curry_val v, const char *who) {
    QByteArray d = data_bytes(v, t->fi->elem, who);
    if (d.size() != t->byte_size())
        curry_error("%s: data is %lld bytes; a %dx%dx%d %s texture needs exactly %lld",
                    who, (long long)d.size(), t->w, t->h, t->d ? t->d : 1, t->fi->name,
                    (long long)t->byte_size());
    return d;
}

/* (make-gpu-texture w h ['format f] ['depth d] ['data data] ['render-target b] ['storage b] ['mipmaps b]) */
static curry_val fn_make_gpu_texture(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "format", "depth", "data", "render-target", "storage", "mipmaps", nullptr };
    intptr_t w = checked_fixnum(av[0], 1, "make-gpu-texture");
    intptr_t h = checked_fixnum(av[1], 2, "make-gpu-texture");
    if (w < 1 || h < 1 || w > 16384 || h > 16384) curry_error("make-gpu-texture: width and height must be in [1, 16384]");
    Opts o = parse_opts(ac, av, 2, "make-gpu-texture", allowed);
    auto *t = new GpuTexture();
    t->fi = find_tex_format(opt_sym(o, "format", "rgba8"), "make-gpu-texture");
    t->w = (int)w; t->h = (int)h;
    t->d = (int)opt_uint(o, "depth", 0, 2048);
    t->render_target = opt_bool(o, "render-target", false);
    t->storage = opt_bool(o, "storage", false);
    t->mipmaps = opt_bool(o, "mipmaps", false);
    if (t->mipmaps && t->d) { delete t; curry_error("make-gpu-texture: 'mipmaps is not supported for 3D textures"); }
    /* Metal's limit for every 3D dimension; it aborts rather than failing. */
    if (t->d && (t->w > 2048 || t->h > 2048 || t->d > 2048)) {
        delete t; curry_error("make-gpu-texture: 3D textures are limited to 2048 in every dimension");
    }
    if (t->mipmaps && is_integer_format(t->fi->fmt)) {
        delete t; curry_error("make-gpu-texture: integer formats cannot have generated mipmaps");
    }
    if ((quint64)t->byte_size() > 0x7fffffffull) { delete t; curry_error("make-gpu-texture: texture larger than 2 GiB"); }
    if (o.has("data")) {
        /* texture_data may raise; don't leak t (no destructors run on longjmp). */
        QByteArray *tmp = nullptr;
        ExnHandler hd;
        curry_val err = 0;
        SCM_PROTECT(hd, tmp = new QByteArray(texture_data(t, o.get("data"), "make-gpu-texture")), err = hd.exn);
        if (err) { delete t; scm_raise_val(err); }
        t->data = *tmp; delete tmp;
        t->dirty = true;
    }
    return ptr_to_val("gpu-texture", t);
}

static curry_val fn_gpu_texture_update(int, curry_val *av, void *) {
    GpuTexture *t = val_to_texture(av[0]);
    t->check_live("gpu-texture-update!");
    t->data = texture_data(t, av[1], "gpu-texture-update!");
    t->dirty = true;
    return curry_void();
}

static curry_val fn_gpu_texture_size(int, curry_val *av, void *) {
    GpuTexture *t = val_to_texture(av[0]);
    return t->d ? curry_list(3, curry_make_fixnum(t->w), curry_make_fixnum(t->h), curry_make_fixnum(t->d))
                : curry_list(2, curry_make_fixnum(t->w), curry_make_fixnum(t->h));
}

/* (make-gpu-sampler ['filter linear|nearest] ['wrap repeat|clamp|mirror] ['mipmaps b]) */
static curry_val fn_make_gpu_sampler(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "filter", "wrap", "mipmaps", nullptr };
    Opts o = parse_opts(ac, av, 0, "make-gpu-sampler", allowed);
    const char *f = opt_sym(o, "filter", "linear");
    const char *w = opt_sym(o, "wrap", "clamp");
    QRhiSampler::Filter filt;
    if      (strcmp(f, "linear") == 0)  filt = QRhiSampler::Linear;
    else if (strcmp(f, "nearest") == 0) filt = QRhiSampler::Nearest;
    else curry_error("make-gpu-sampler: 'filter must be linear or nearest");
    QRhiSampler::AddressMode wrap;
    if      (strcmp(w, "repeat") == 0) wrap = QRhiSampler::Repeat;
    else if (strcmp(w, "clamp") == 0)  wrap = QRhiSampler::ClampToEdge;
    else if (strcmp(w, "mirror") == 0) wrap = QRhiSampler::Mirror;
    else curry_error("make-gpu-sampler: 'wrap must be repeat, clamp or mirror");
    auto *s = new GpuSampler();
    s->filter = filt;
    s->wrap = wrap;
    s->mip = opt_bool(o, "mipmaps", false) ? filt : QRhiSampler::None;
    return ptr_to_val("gpu-sampler", s);
}

/* The GLSL front end and the SPIR-V translators recurse once per nested
 * expression node, so a long enough expression (`1.0+1.0+...`) overflows
 * the 8 MB main-thread stack and kills the process.  Compiling on a
 * thread with a 512 MB stack (address space only; untouched pages cost
 * nothing) together with the source size limit puts the deepest possible
 * tree well inside the stack. */
static const size_t k_max_shader_source = 256 * 1024;

struct BakeJob { QShaderBaker *baker; QShader result; bool done; };

static void *bake_thread_main(void *arg) {
    BakeJob *j = (BakeJob *)arg;
    j->result = j->baker->bake();
    j->done = true;
    return nullptr;
}

static bool bake_on_big_stack(BakeJob *job) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return false;
    pthread_attr_setstacksize(&attr, (size_t)512 << 20);
    pthread_t th;
    int rc = pthread_create(&th, &attr, bake_thread_main, job);
    pthread_attr_destroy(&attr);
    if (rc != 0) return false;
    pthread_join(th, nullptr);
    return job->done;
}

/* (make-gpu-shader stage source) — Vulkan-style GLSL, compiled now. */
static curry_val fn_make_gpu_shader(int, curry_val *av, void *) {
    const char *stage = checked_symbol(av[0], 1, "make-gpu-shader");
    const char *src = checked_string(av[1], 2, "make-gpu-shader");
    QShader::Stage st;
    if      (strcmp(stage, "vertex") == 0)   st = QShader::VertexStage;
    else if (strcmp(stage, "fragment") == 0) st = QShader::FragmentStage;
    else if (strcmp(stage, "compute") == 0)  st = QShader::ComputeStage;
    else curry_error("make-gpu-shader: stage must be vertex, fragment or compute");

    size_t src_len = strlen(src);
    if (src_len > k_max_shader_source)
        curry_error("make-gpu-shader: shader source is %zu bytes; the limit is %zu", src_len, k_max_shader_source);

    QShaderBaker baker;
    QList<QShaderBaker::GeneratedShader> targets = {
        { QShader::SpirvShader, QShaderVersion(100) },
        { QShader::MslShader,   QShaderVersion(12) },
        { QShader::HlslShader,  QShaderVersion(50) },
    };
    if (st == QShader::ComputeStage) {
        targets.append({ QShader::GlslShader, QShaderVersion(430) });
        targets.append({ QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs) });
    } else {
        targets.append({ QShader::GlslShader, QShaderVersion(330) });
        targets.append({ QShader::GlslShader, QShaderVersion(430) });
        targets.append({ QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs) });
    }
    baker.setGeneratedShaders(targets);
    baker.setGeneratedShaderVariants({ QShader::StandardShader });
    /* A target a shader genuinely can't be translated to (e.g. a feature
     * GLSL ES lacks) shouldn't block the others; the missing variant only
     * matters if that backend is actually used, and QRhi reports it then. */
    baker.setPerTargetCompilation(true);
    baker.setBreakOnShaderTranslationError(false);
    baker.setSourceString(QByteArray(src, (qsizetype)src_len), st);
    BakeJob job{ &baker, QShader(), false };
    if (!bake_on_big_stack(&job)) curry_error("make-gpu-shader: could not start the shader compiler thread");
    QShader sh = job.result;
    if (!sh.isValid()) {
        QByteArray msg = baker.errorMessage().toUtf8();
        curry_error("make-gpu-shader: %s shader failed to compile:\n%s", stage, msg.constData());
    }
    return ptr_to_val("gpu-shader", new GpuShader{ st, sh });
}

static QRhiShaderResourceBinding::StageFlags parse_stages(curry_val v, const char *who) {
    QRhiShaderResourceBinding::StageFlags f;
    curry_val lst = curry_is_symbol(v) ? curry_make_pair(v, curry_nil()) : v;
    if (list_length(lst) <= 0) curry_error("%s: stages must be a symbol or non-empty list of symbols", who);
    for (curry_val p = lst; curry_is_pair(p); p = curry_cdr(p)) {
        curry_val s = curry_car(p);
        if      (sym_is(s, "vertex"))   f |= QRhiShaderResourceBinding::VertexStage;
        else if (sym_is(s, "fragment")) f |= QRhiShaderResourceBinding::FragmentStage;
        else if (sym_is(s, "compute"))  f |= QRhiShaderResourceBinding::ComputeStage;
        else curry_error("%s: unknown stage (expected vertex, fragment or compute)", who);
    }
    return f;
}

/* (make-gpu-bindings entries) — entries:
 *   (uniform  binding stages buffer)
 *   (texture  binding stages texture sampler)
 *   (storage-read | storage-write | storage  binding stages buffer)
 *   (image-read   | image-write   | image    binding stages texture [mip-level]) */
static curry_val fn_make_gpu_bindings(int, curry_val *av, void *) {
    const char *who = "make-gpu-bindings";
    if (list_length(av[0]) < 0) curry_error("%s: argument must be a list of binding entries", who);
    std::vector<BindEntry> entries;   /* a raise below leaks this vector's storage (small) */
    entries.reserve((size_t)list_length(av[0]));
    for (curry_val p = av[0]; curry_is_pair(p); p = curry_cdr(p)) {
        curry_val e = curry_car(p);
        long n = list_length(e);
        if (n < 4 || !curry_is_symbol(curry_car(e)))
            curry_error("%s: each entry is (kind binding stages resource ...)", who);
        const char *k = curry_symbol(curry_car(e));
        curry_val binding = curry_car(curry_cdr(e));
        curry_val stages = curry_car(curry_cdr(curry_cdr(e)));
        curry_val res = curry_car(curry_cdr(curry_cdr(curry_cdr(e))));
        curry_val rest = curry_cdr(curry_cdr(curry_cdr(curry_cdr(e))));
        if (!curry_is_fixnum(binding) || curry_fixnum(binding) < 0 || curry_fixnum(binding) > 1023)
            curry_error("%s: binding number must be an exact integer in [0, 1023]", who);
        BindEntry be{};
        be.binding = (int)curry_fixnum(binding);
        be.stages = parse_stages(stages, who);
        static const struct { const char *n; BindKind k; bool tex; } kinds[] = {
            { "uniform", BindKind::Uniform, false }, { "texture", BindKind::Texture, true },
            { "storage-read", BindKind::StorageRead, false }, { "storage-write", BindKind::StorageWrite, false },
            { "storage", BindKind::Storage, false }, { "image-read", BindKind::ImageRead, true },
            { "image-write", BindKind::ImageWrite, true }, { "image", BindKind::Image, true },
        };
        bool found = false, is_tex = false;
        for (auto &kk : kinds) if (strcmp(kk.n, k) == 0) { be.kind = kk.k; is_tex = kk.tex; found = true; }
        if (!found) curry_error("%s: unknown binding kind '%s", who, k);
        if (is_tex) {
            be.tex = val_to_texture(res);
            if (be.kind == BindKind::Texture) {
                if (n != 5) curry_error("%s: (texture binding stages texture sampler)", who);
                be.smp = val_to_sampler(curry_car(rest));
            } else {
                if (!be.tex->storage) curry_error("%s: image bindings need a texture made with 'storage #t", who);
                if (n == 5) {
                    if (!curry_is_fixnum(curry_car(rest)) || curry_fixnum(curry_car(rest)) < 0)
                        curry_error("%s: mip level must be a non-negative exact integer", who);
                    be.level = (int)curry_fixnum(curry_car(rest));
                    int levels = 1;
                    if (be.tex->mipmaps)
                        for (int sz = std::max(be.tex->w, be.tex->h); sz > 1; sz >>= 1) levels++;
                    if (be.level >= levels)
                        curry_error("%s: mip level %d does not exist; the texture has %d level(s)", who, be.level, levels);
                } else if (n != 4) curry_error("%s: (%s binding stages texture [level])", who, k);
            }
        } else {
            if (n != 4) curry_error("%s: (%s binding stages buffer)", who, k);
            be.buf = val_to_buffer(res);
            bool want_uniform = be.kind == BindKind::Uniform;
            if (want_uniform && !(be.buf->usage & QRhiBuffer::UniformBuffer))
                curry_error("%s: uniform binding %d needs a buffer made with usage 'uniform", who, be.binding);
            if (!want_uniform && !(be.buf->usage & QRhiBuffer::StorageBuffer))
                curry_error("%s: storage binding %d needs a buffer made with usage 'storage", who, be.binding);
        }
        for (auto &prev : entries)
            if (prev.binding == be.binding) curry_error("%s: binding %d appears twice", who, be.binding);
        entries.push_back(be);
    }
    auto *b = new GpuBindings();
    b->entries = std::move(entries);
    return ptr_to_val("gpu-bindings", b);
}

/* ---- checking bindings and vertex inputs against the shaders --------------
 *
 * QShader carries reflection data for every resource a shader declares.
 * Metal aborts the whole process (MTLReportFailure) when a pipeline or
 * draw does not match its shaders -- a missing buffer, a uniform buffer
 * smaller than its block, a 2D texture where the shader samples a 3D one,
 * a storage image of a different format -- so all of that is checked here
 * and reported as an ordinary Scheme error instead.
 */

static QShaderDescription::ImageFormat image_format_of(QRhiTexture::Format f) {
    using D = QShaderDescription;
    switch (f) {
    case QRhiTexture::RGBA8:    return D::ImageFormatRgba8;
    case QRhiTexture::R8:       return D::ImageFormatR8;
    case QRhiTexture::RG8:      return D::ImageFormatRg8;
    case QRhiTexture::R16F:     return D::ImageFormatR16f;
    case QRhiTexture::RGBA16F:  return D::ImageFormatRgba16f;
    case QRhiTexture::R32F:     return D::ImageFormatR32f;
    case QRhiTexture::RGBA32F:  return D::ImageFormatRgba32f;
    case QRhiTexture::R32UI:    return D::ImageFormatR32ui;
    case QRhiTexture::R32SI:    return D::ImageFormatR32i;
    case QRhiTexture::RGBA32UI: return D::ImageFormatRgba32ui;
    case QRhiTexture::RGBA32SI: return D::ImageFormatRgba32i;
    default:                    return D::ImageFormatUnknown;
    }
}

static bool is_integer_format(QRhiTexture::Format f) {
    return f == QRhiTexture::R32UI || f == QRhiTexture::R32SI ||
           f == QRhiTexture::RGBA32UI || f == QRhiTexture::RGBA32SI;
}

static const BindEntry *entry_at(const GpuBindings *b, int binding) {
    for (auto &e : b->entries) if (e.binding == binding) return &e;
    return nullptr;
}

static void check_texture_dim(const GpuTexture *t, QShaderDescription::VariableType vt,
                              int binding, const char *who, const char *stage) {
    using D = QShaderDescription;
    bool want3d;
    if      (vt == D::Sampler2D || vt == D::Image2D) want3d = false;
    else if (vt == D::Sampler3D || vt == D::Image3D) want3d = true;
    else curry_error("%s: %s shader binding %d uses a texture type gpu-* does not support yet "
                     "(only 2D and 3D)", who, stage, binding);
    if (want3d != (t->d != 0))
        curry_error("%s: %s shader binding %d is a %s texture but a %s texture is bound",
                    who, stage, binding, want3d ? "3D" : "2D", t->d ? "3D" : "2D");
}

/* Every resource `sh` declares must be in `b`, with the right kind, the
 * stage enabled, and big enough. */
static void check_shader_resources(const GpuShader *sh, const GpuBindings *b, const char *who) {
    using SRB = QRhiShaderResourceBinding;
    const QShaderDescription desc = sh->shader.description();
    const char *stage = sh->stage == QShader::VertexStage ? "vertex"
                      : sh->stage == QShader::FragmentStage ? "fragment" : "compute";
    SRB::StageFlag flag = sh->stage == QShader::VertexStage ? SRB::VertexStage
                        : sh->stage == QShader::FragmentStage ? SRB::FragmentStage : SRB::ComputeStage;
    if (!desc.pushConstantBlocks().isEmpty())
        curry_error("%s: %s shader uses push constants, which gpu-* does not support; use a uniform block", who, stage);
    if (!desc.separateImages().isEmpty() || !desc.separateSamplers().isEmpty())
        curry_error("%s: %s shader uses separate texture/sampler objects; use a combined sampler2D/sampler3D", who, stage);

    auto need = [&](int binding, int set, const char *what) -> const BindEntry * {
        if (set > 0) curry_error("%s: %s shader %s at binding %d uses descriptor set %d; only set 0 is supported",
                                 who, stage, what, binding, set);
        const BindEntry *e = entry_at(b, binding);
        if (!e) curry_error("%s: %s shader declares a %s at binding %d but the bindings have nothing there",
                            who, stage, what, binding);
        if (!(e->stages & flag))
            curry_error("%s: binding %d is used by the %s shader but its stages do not include %s",
                        who, binding, stage, stage);
        return e;
    };
    for (const auto &ub : desc.uniformBlocks()) {
        const BindEntry *e = need(ub.binding, ub.descriptorSet, "uniform block");
        if (e->kind != BindKind::Uniform)
            curry_error("%s: %s shader binding %d is a uniform block but something else is bound", who, stage, ub.binding);
        if (e->buf->data.size() < ub.size)
            curry_error("%s: uniform block '%s' at binding %d is %d bytes but the bound buffer is %lld bytes",
                        who, ub.blockName.constData(), ub.binding, ub.size, (long long)e->buf->data.size());
    }
    for (const auto &sb : desc.storageBlocks()) {
        const BindEntry *e = need(sb.binding, sb.descriptorSet, "storage block");
        if (e->kind != BindKind::Storage && e->kind != BindKind::StorageRead && e->kind != BindKind::StorageWrite)
            curry_error("%s: %s shader binding %d is a storage block but something else is bound", who, stage, sb.binding);
        if (e->buf->data.size() < sb.knownSize)
            curry_error("%s: storage block '%s' at binding %d needs at least %d bytes but the bound buffer is %lld bytes",
                        who, sb.blockName.constData(), sb.binding, sb.knownSize, (long long)e->buf->data.size());
    }
    auto no_arrays = [&](const QShaderDescription::InOutVariable &v, const char *what) {
        if (!v.arrayDims.isEmpty())
            curry_error("%s: %s shader declares an array of %ss at binding %d; gpu-* binds one resource per binding",
                        who, stage, what, v.binding);
    };
    for (const auto &v : desc.combinedImageSamplers()) {
        no_arrays(v, "texture");
        const BindEntry *e = need(v.binding, v.descriptorSet, "sampled texture");
        if (e->kind != BindKind::Texture)
            curry_error("%s: %s shader binding %d is a sampled texture but something else is bound", who, stage, v.binding);
        if (is_integer_format(e->tex->fi->fmt))
            curry_error("%s: binding %d: integer-format textures (%s) cannot be sampled; bind them as an image",
                        who, v.binding, e->tex->fi->name);
        check_texture_dim(e->tex, v.type, v.binding, who, stage);
    }
    for (const auto &v : desc.storageImages()) {
        no_arrays(v, "image");
        const BindEntry *e = need(v.binding, v.descriptorSet, "storage image");
        if (e->kind != BindKind::Image && e->kind != BindKind::ImageRead && e->kind != BindKind::ImageWrite)
            curry_error("%s: %s shader binding %d is a storage image but something else is bound", who, stage, v.binding);
        check_texture_dim(e->tex, v.type, v.binding, who, stage);
        if (v.imageFormat != image_format_of(e->tex->fi->fmt))
            curry_error("%s: %s shader image at binding %d has a different format from the bound %s texture",
                        who, stage, v.binding, e->tex->fi->name);
    }
}

/* Bindings swapped in at draw/dispatch time must have the pipeline's
 * layout: same binding numbers, kinds and stages. */
static void check_same_layout(const GpuBindings *layout, const GpuBindings *b, const char *who) {
    if (layout == b) return;
    if (layout->entries.size() != b->entries.size())
        curry_error("%s: 'bindings has %zu entries but the pipeline's layout has %zu",
                    who, b->entries.size(), layout->entries.size());
    for (const auto &le : layout->entries) {
        const BindEntry *e = entry_at(b, le.binding);
        if (!e || e->kind != le.kind || e->stages != le.stages)
            curry_error("%s: 'bindings entry %d does not match the pipeline's layout (kind and stages must be the same)",
                        who, le.binding);
    }
}

/* Every vertex shader input must be fed by an attribute in 'inputs. */
static void check_vertex_inputs(const GpuShader *vs, const GpuPipeline *p, const char *who) {
    using D = QShaderDescription;
    for (const auto &in : vs->shader.description().inputVariables()) {
        /* A matrix or array input takes several locations; only one
         * attribute per input is supported, so refuse them outright. */
        switch (in.type) {
        case D::Mat2: case D::Mat2x3: case D::Mat2x4: case D::Mat3: case D::Mat3x2: case D::Mat3x4:
        case D::Mat4: case D::Mat4x2: case D::Mat4x3:
        case D::DMat2: case D::DMat2x3: case D::DMat2x4: case D::DMat3: case D::DMat3x2: case D::DMat3x4:
        case D::DMat4: case D::DMat4x2: case D::DMat4x3:
            curry_error("%s: vertex input '%s' is a matrix; pass its columns as separate vec4 inputs",
                        who, in.name.constData());
        default: break;
        }
        if (!in.arrayDims.isEmpty())
            curry_error("%s: vertex input '%s' is an array; arrays of vertex inputs are not supported",
                        who, in.name.constData());
        bool found = false;
        for (auto &a : p->vattrs) if (a.location == in.location) found = true;
        if (!found)
            curry_error("%s: the vertex shader reads input '%s' at location %d but 'inputs has no attribute there",
                        who, in.name.constData(), in.location);
    }
}

static GpuBindings *s_empty_bindings = nullptr;
static GpuBindings *empty_bindings() {
    if (!s_empty_bindings) s_empty_bindings = new GpuBindings();
    return s_empty_bindings;
}

struct AttrFormat { const char *name; QRhiVertexInputAttribute::Format f; quint32 size; };
static const AttrFormat k_attr_formats[] = {
    { "float", QRhiVertexInputAttribute::Float, 4 },    { "float2", QRhiVertexInputAttribute::Float2, 8 },
    { "float3", QRhiVertexInputAttribute::Float3, 12 }, { "float4", QRhiVertexInputAttribute::Float4, 16 },
    { "uint", QRhiVertexInputAttribute::UInt, 4 },      { "uint2", QRhiVertexInputAttribute::UInt2, 8 },
    { "uint3", QRhiVertexInputAttribute::UInt3, 12 },   { "uint4", QRhiVertexInputAttribute::UInt4, 16 },
    { "sint", QRhiVertexInputAttribute::SInt, 4 },      { "sint2", QRhiVertexInputAttribute::SInt2, 8 },
    { "sint3", QRhiVertexInputAttribute::SInt3, 12 },   { "sint4", QRhiVertexInputAttribute::SInt4, 16 },
    { "unorm-byte4", QRhiVertexInputAttribute::UNormByte4, 4 },
};

/* Conservative limits every backend accepts (Metal shares its 31 buffer
 * slots between vertex buffers and shader resources). */
static const int k_max_vertex_slots = 8;
static const int k_max_vertex_attrs = 16;

/* 'inputs: list of vertex buffer slots, each
 *   (stride-bytes [per-instance] (location format offset-bytes) ...) */
static void parse_inputs(curry_val v, GpuPipeline *p, const char *who) {
    if (list_length(v) < 0) curry_error("%s: 'inputs must be a list", who);
    int bi = 0;
    for (curry_val s = v; curry_is_pair(s); s = curry_cdr(s), bi++) {
        curry_val slot = curry_car(s);
        if (list_length(slot) < 1 || !curry_is_fixnum(curry_car(slot)) ||
            curry_fixnum(curry_car(slot)) <= 0 || curry_fixnum(curry_car(slot)) > 2048)
            curry_error("%s: input slot %d must start with a stride in bytes, in [1, 2048]", who, bi);
        if (bi >= k_max_vertex_slots) curry_error("%s: at most %d vertex buffer slots", who, k_max_vertex_slots);
        VInputBinding b{ (quint32)curry_fixnum(curry_car(slot)), false, 0 };
        curry_val a = curry_cdr(slot);
        if (curry_is_pair(a) && sym_is(curry_car(a), "per-instance")) { b.per_instance = true; a = curry_cdr(a); }
        for (; curry_is_pair(a); a = curry_cdr(a)) {
            curry_val at = curry_car(a);
            if (list_length(at) != 3 || !curry_is_fixnum(curry_car(at)) || !curry_is_symbol(curry_car(curry_cdr(at))) ||
                !curry_is_fixnum(curry_car(curry_cdr(curry_cdr(at)))))
                curry_error("%s: attributes are (location format offset-bytes)", who);
            intptr_t loc = curry_fixnum(curry_car(at)), off = curry_fixnum(curry_car(curry_cdr(curry_cdr(at))));
            const char *fn = curry_symbol(curry_car(curry_cdr(at)));
            const AttrFormat *af = nullptr;
            for (auto &f : k_attr_formats) if (strcmp(f.name, fn) == 0) af = &f;
            if (!af) curry_error("%s: unknown attribute format '%s", who, fn);
            if (loc < 0 || loc >= k_max_vertex_attrs)
                curry_error("%s: attribute location must be in [0, %d]", who, k_max_vertex_attrs - 1);
            if (off < 0 || (quint64)off + af->size > b.stride)
                curry_error("%s: attribute at location %ld (%s at offset %ld) does not fit in the %u-byte stride",
                            who, (long)loc, fn, (long)off, b.stride);
            for (auto &prev : p->vattrs)
                if (prev.location == (int)loc) curry_error("%s: vertex attribute location %ld is used twice", who, (long)loc);
            p->vattrs.push_back({ bi, (int)loc, af->f, (quint32)off });
            if ((quint32)off + af->size > b.attr_end) b.attr_end = (quint32)off + af->size;
        }
        p->vbindings.push_back(b);
    }
}

/* (make-gpu-pipeline 'vertex vs 'fragment fs ['bindings b] ['inputs layout]
 *                    ['topology t] ['depth-test b] ['depth-write b] ['depth-op op]
 *                    ['cull c] ['front-face f] ['blend m] ['polygon p] ['line-width w]) */
static curry_val fn_make_gpu_pipeline(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "vertex", "fragment", "bindings", "inputs", "topology",
        "depth-test", "depth-write", "depth-op", "cull", "front-face", "blend", "polygon", "line-width", nullptr };
    const char *who = "make-gpu-pipeline";
    Opts o = parse_opts(ac, av, 0, who, allowed);
    if (!o.has("vertex") || !o.has("fragment")) curry_error("%s: 'vertex and 'fragment shaders are required", who);
    GpuShader *vs = val_to_shader(o.get("vertex"));
    GpuShader *fs = val_to_shader(o.get("fragment"));
    if (vs->stage != QShader::VertexStage) curry_error("%s: 'vertex is not a vertex shader", who);
    if (fs->stage != QShader::FragmentStage) curry_error("%s: 'fragment is not a fragment shader", who);
    GpuBindings *layout = o.has("bindings") ? val_to_bindings(o.get("bindings")) : empty_bindings();

    using G = QRhiGraphicsPipeline;
    static const struct { const char *n; G::Topology t; } topo[] = {
        { "triangles", G::Triangles }, { "triangle-strip", G::TriangleStrip }, { "triangle-fan", G::TriangleFan },
        { "lines", G::Lines }, { "line-strip", G::LineStrip }, { "points", G::Points } };
    static const struct { const char *n; G::CompareOp op; } ops[] = {
        { "never", G::Never }, { "less", G::Less }, { "equal", G::Equal }, { "less-or-equal", G::LessOrEqual },
        { "greater", G::Greater }, { "not-equal", G::NotEqual }, { "greater-or-equal", G::GreaterOrEqual },
        { "always", G::Always } };
    const char *ts = opt_sym(o, "topology", "triangles");
    const char *dop = opt_sym(o, "depth-op", "less-or-equal");
    const char *cull = opt_sym(o, "cull", "none");
    const char *ff = opt_sym(o, "front-face", "ccw");
    const char *blend = opt_sym(o, "blend", "none");
    const char *poly = opt_sym(o, "polygon", "fill");
    double lw = opt_real(o, "line-width", 1.0);
    if (!(lw > 0.0 && lw <= 64.0)) curry_error("%s: 'line-width must be in (0, 64]", who);

    auto p = std::make_unique<GpuPipeline>();
    p->vs = vs; p->fs = fs; p->layout = layout;
    bool ok = false;
    for (auto &t : topo) if (strcmp(t.n, ts) == 0) { p->topology = t.t; ok = true; }
    if (!ok) curry_error("%s: unknown 'topology '%s", who, ts);
    ok = false;
    for (auto &c : ops) if (strcmp(c.n, dop) == 0) { p->depth_op = c.op; ok = true; }
    if (!ok) curry_error("%s: unknown 'depth-op '%s", who, dop);
    if      (strcmp(cull, "none") == 0)  p->cull = G::None;
    else if (strcmp(cull, "back") == 0)  p->cull = G::Back;
    else if (strcmp(cull, "front") == 0) p->cull = G::Front;
    else curry_error("%s: 'cull must be none, back or front", who);
    if      (strcmp(ff, "ccw") == 0) p->front = G::CCW;
    else if (strcmp(ff, "cw") == 0)  p->front = G::CW;
    else curry_error("%s: 'front-face must be ccw or cw", who);
    if      (strcmp(blend, "none") == 0)     p->blend = 0;
    else if (strcmp(blend, "alpha") == 0)    p->blend = 1;
    else if (strcmp(blend, "additive") == 0) p->blend = 2;
    else curry_error("%s: 'blend must be none, alpha or additive", who);
    if      (strcmp(poly, "fill") == 0) p->polygon = G::Fill;
    else if (strcmp(poly, "line") == 0) p->polygon = G::Line;
    else curry_error("%s: 'polygon must be fill or line", who);
    p->depth_test = opt_bool(o, "depth-test", true);
    p->depth_write = opt_bool(o, "depth-write", true);
    p->line_width = (float)lw;
    if (o.has("inputs")) parse_inputs(o.get("inputs"), p.get(), who);
    check_vertex_inputs(vs, p.get(), who);
    check_shader_resources(vs, layout, who);
    check_shader_resources(fs, layout, who);
    /* curry_error longjmps past p's destructor: everything that can raise is
     * done, release ownership to the handle now. */
    return ptr_to_val("gpu-pipeline", p.release());
}

/* (make-gpu-compute-pipeline cs [bindings]) */
static curry_val fn_make_gpu_compute_pipeline(int ac, curry_val *av, void *) {
    GpuShader *cs = val_to_shader(av[0]);
    if (cs->stage != QShader::ComputeStage) curry_error("make-gpu-compute-pipeline: not a compute shader");
    GpuBindings *layout = ac > 1 ? val_to_bindings(av[1]) : empty_bindings();
    check_shader_resources(cs, layout, "make-gpu-compute-pipeline");
    auto *c = new GpuCompute();
    c->cs = cs; c->layout = layout;
    return ptr_to_val("gpu-compute-pipeline", c);
}

/* (gpu-release! resource) */
static curry_val fn_gpu_release(int, curry_val *av, void *) {
    forbid_during_frame("gpu-release!");
    curry_val v = av[0];
    if (!curry_is_pair(v) || !curry_is_symbol(curry_car(v))) curry_error("gpu-release!: not a gpu resource");
    const char *tag = curry_symbol(curry_car(v));
    GpuRes *r = nullptr;
    if      (!strcmp(tag, "gpu-buffer"))           r = val_to_buffer(v);
    else if (!strcmp(tag, "gpu-texture"))          r = val_to_texture(v);
    else if (!strcmp(tag, "gpu-sampler"))          r = val_to_sampler(v);
    else if (!strcmp(tag, "gpu-bindings"))         r = val_to_bindings(v);
    else if (!strcmp(tag, "gpu-pipeline"))         r = val_to_pipeline(v);
    else if (!strcmp(tag, "gpu-compute-pipeline")) r = val_to_compute(v);
    else curry_error("gpu-release!: not a releasable gpu resource");
    /* Natives go now; the small C++ shell stays so later use raises
     * "has been released" instead of touching freed memory.  CPU-side data
     * is freed too. */
    r->detach();
    r->released = true;
    if (auto *b = dynamic_cast<GpuBuffer *>(r)) b->data = QByteArray();
    if (auto *t = dynamic_cast<GpuTexture *>(r)) t->data.clear();
    return curry_void();
}

/* =========================================================================
 * Frames and passes
 * ========================================================================= */

/* Run proc with one argument under SCM_PROTECT.  Returns 0 and sets *result,
 * or returns the exception value. */
static curry_val protected_apply1(curry_val proc, curry_val arg, curry_val *result) {
    ExnHandler h;
    curry_val err = 0;
    curry_val r = curry_void();
    SCM_PROTECT(h, r = curry_apply(proc, 1, &arg), err = h.exn);
    *result = r;
    return err;
}

/* ---- call/cc escapes ---------------------------------------------------------
 *
 * curry's call/cc is escape-only and jumps straight out with longjmp,
 * skipping exception handlers -- so the cleanup after protected_apply1
 * never runs and the frame/pass would stay "active" with QRhi mid-frame.
 * Escapes do run dynamic-wind after-thunks (wind_unwind_to), so frame and
 * pass procedures run under a WindFrame whose after-thunk is a C primitive
 * that ends the pass or the device frame.  On a normal return or a Scheme
 * error the WindFrame is just popped.
 */

static curry_val s_noop_prim = 0, s_end_pass_prim = 0, s_end_device_frame_prim = 0;

static curry_val make_prim(const char *name, CurryFn fn) {
    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->min_args = 0; p->max_args = 0;
    p->fn = (PrimFn)fn; p->ud = nullptr;
    return vptr(p);
}

static WindFrame *push_wind(curry_val after) {
    WindFrame *wf = (WindFrame *)gc_alloc_raw_pinned(sizeof(WindFrame));
    wf->before = s_noop_prim;
    wf->after = after;
    wf->prev = current_wind;
    current_wind = wf;
    return wf;
}

static void pop_wind(WindFrame *wf) { current_wind = wf->prev; }

static void free_readbacks(std::vector<Readback *> &rbs, size_t from = 0) {
    for (size_t j = from; j < rbs.size(); j++) {
        if (!rbs[j]) continue;
        if (rbs[j]->proc_slot) { gc_unregister_root(rbs[j]->proc_slot); delete rbs[j]->proc_slot; }
        delete rbs[j];
        rbs[j] = nullptr;
    }
}

static curry_val fn_noop(int, curry_val *, void *) { return curry_void(); }

static curry_val fn_end_pass_on_escape(int, curry_val *, void *) {
    if (s_frame && s_frame->pass) { delete s_frame->pass; s_frame->pass = nullptr; }
    return curry_void();
}

static curry_val fn_end_canvas_frame_on_escape(int, curry_val *, void *) {
    if (s_frame && !s_frame->device_frame) {
        if (s_frame->pass) { delete s_frame->pass; s_frame->pass = nullptr; }
        s_frame = nullptr;
    }
    return curry_void();
}

static curry_val fn_end_device_frame_on_escape(int, curry_val *, void *) {
    if (!s_frame || !s_frame->device_frame) return curry_void();
    FrameRec *f = s_frame;
    s_frame = nullptr;
    if (f->pass) { delete f->pass; f->pass = nullptr; }
    f->rhi->endOffscreenFrame();
    free_readbacks(f->device_readbacks);
    delete f;
    return curry_void();
}

/* (gpu-run! device (lambda (frame) ...)) — one synchronous offscreen frame. */
static curry_val fn_gpu_run(int, curry_val *av, void *) {
    QRhi *rhi = device_rhi(val_to_device(av[0]), "gpu-run!");
    if (!curry_is_procedure(av[1])) curry_error("gpu-run!: argument 2 must be a procedure");
    forbid_during_frame("gpu-run!");
    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        curry_error("gpu-run!: could not begin an offscreen frame");
    auto *f = new FrameRec{};
    f->serial = ++s_serial;
    f->rhi = rhi;
    f->cb = cb;
    f->device_frame = true;
    s_frame = f;
    curry_val result;
    WindFrame *wf = push_wind(s_end_device_frame_prim);
    curry_val err = protected_apply1(av[1], serial_handle("gpu-frame", f->serial), &result);
    pop_wind(wf);
    s_frame = nullptr;
    rhi->endOffscreenFrame();   /* waits for completion: readbacks are done */
    std::vector<Readback *> rbs = std::move(f->device_readbacks);
    delete f;
    if (err) {
        free_readbacks(rbs);
        scm_raise_val(err);
    }
    /* Deliver async readbacks now, outside Qt's code, so their errors
     * propagate normally.  Each Readback is freed before calling its proc. */
    for (size_t i = 0; i < rbs.size(); i++) {
        Readback *rb = rbs[i];
        curry_val bv = bytes_to_bv(rb->result.data);
        curry_val proc = *rb->proc_slot;
        gc_unregister_root(rb->proc_slot); delete rb->proc_slot; delete rb;
        rbs[i] = nullptr;
        curry_val ignored;
        curry_val e2 = protected_apply1(proc, bv, &ignored);
        if (e2) {
            free_readbacks(rbs, i + 1);
            scm_raise_val(e2);
        }
    }
    return result;
}

static QColor parse_color(curry_val v, const char *who) {
    if (list_length(v) != 4) curry_error("%s: 'clear must be a list (r g b a) of numbers in [0, 1]", who);
    double c[4]; int i = 0;
    for (curry_val p = v; curry_is_pair(p); p = curry_cdr(p), i++) {
        if (!curry_is_number(curry_car(p))) curry_error("%s: 'clear components must be numbers", who);
        c[i] = curry_number_to_double(curry_car(p));
    }
    return QColor::fromRgbF((float)c[0], (float)c[1], (float)c[2], (float)c[3]);
}

static void check_draw_ranges(const DrawCmd &d, const char *who);

static void execute_render_pass(FrameRec *f, PassRec *pr, QRhiRenderTarget *rt, GpuTexture *target_tex,
                                const QColor &clear, float depth_clear) {
    QRhi *rhi = f->rhi;
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    if (!u) curry_error("gpu-render-pass!: out of resource update batches");
    batch_begin();
    /* Everything that can raise happens before beginPass.  On a raise the
     * batch must go back to the pool, or the pool runs dry. */
    ExnHandler h;
    curry_val err = 0;
    std::vector<QRhiGraphicsPipeline *> pss(pr->draws.size());
    SCM_PROTECT(h, {
        if (target_tex) rt = mat_texture_target(rhi, target_tex, u);
        for (size_t i = 0; i < pr->draws.size(); i++) {
            DrawCmd &d = pr->draws[i];
            pss[i] = mat_pipeline(rhi, d.pl, rt, u);
            mat_bindings(rhi, d.b, u);
            for (auto &vb : d.vbufs) mat_buffer(rhi, vb.first, u);
            if (d.ibuf) mat_buffer(rhi, d.ibuf, u);
            /* Again, with the data actually being uploaded: an index
             * buffer updated after gpu-draw! recorded this draw must not
             * get past the check (review of #347). */
            check_draw_ranges(d, "gpu-render-pass!");
            if (target_tex && is_integer_format(target_tex->fi->fmt) && d.pl->blend)
                curry_error("gpu-render-pass!: blending into an integer-format target is not possible");
        }
    }, err = h.exn);
    if (err) { batch_failed(); u->release(); scm_raise_val(err); }

    QRhiCommandBuffer *cb = f->cb;
    cb->beginPass(rt, clear, { depth_clear, 0 }, u);
    QSize sz = rt->pixelSize();
    for (size_t i = 0; i < pr->draws.size(); i++) {
        DrawCmd &d = pr->draws[i];
        cb->setGraphicsPipeline(pss[i]);
        if (d.has_vp) cb->setViewport({ d.vp[0], d.vp[1], d.vp[2], d.vp[3] });
        else          cb->setViewport({ 0, 0, (float)sz.width(), (float)sz.height() });
        if (d.has_sc) {
            /* Metal aborts on a scissor rect outside the target: clip it. */
            int x0 = std::max(0, d.sc[0]), y0 = std::max(0, d.sc[1]);
            int x1 = std::min(sz.width(),  (int)std::min<qint64>((qint64)d.sc[0] + d.sc[2], INT_MAX));
            int y1 = std::min(sz.height(), (int)std::min<qint64>((qint64)d.sc[1] + d.sc[3], INT_MAX));
            if (x1 <= x0 || y1 <= y0) continue;   /* nothing visible: skip the draw */
            cb->setScissor({ x0, y0, x1 - x0, y1 - y0 });
        } else {
            cb->setScissor({ 0, 0, sz.width(), sz.height() });
        }
        cb->setShaderResources(d.b->srb);
        std::vector<QRhiCommandBuffer::VertexInput> vin;
        for (auto &vb : d.vbufs) vin.push_back({ vb.first->buf, vb.second });
        if (d.ibuf) {
            cb->setVertexInput(0, (int)vin.size(), vin.data(), d.ibuf->buf, d.ioff, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(d.count, d.instances, d.first, d.vertex_offset, d.first_instance);
        } else {
            if (!vin.empty()) cb->setVertexInput(0, (int)vin.size(), vin.data());
            cb->draw(d.count, d.instances, d.first, d.first_instance);
        }
    }
    cb->endPass();
}

/* (gpu-render-pass! frame proc ['target tex] ['clear (r g b a)] ['depth-clear d]) */
static curry_val fn_gpu_render_pass(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "target", "clear", "depth-clear", nullptr };
    const char *who = "gpu-render-pass!";
    FrameRec *f = val_to_frame(av[0], who);
    if (!curry_is_procedure(av[1])) curry_error("%s: argument 2 must be a procedure", who);
    if (f->pass) curry_error("%s: passes cannot be nested", who);
    Opts o = parse_opts(ac, av, 2, who, allowed);
    GpuTexture *target = o.has("target") ? val_to_texture(o.get("target")) : nullptr;
    if (!target && f->device_frame) curry_error("%s: a device frame needs a 'target texture", who);
    if (target) target->check_live(who);
    QColor clear = o.has("clear") ? parse_color(o.get("clear"), who) : QColor(0, 0, 0, 255);
    double dclear_d = opt_real(o, "depth-clear", 1.0);
    if (!(dclear_d >= 0.0 && dclear_d <= 1.0)) curry_error("%s: 'depth-clear must be in [0, 1]", who);
    float dclear = (float)dclear_d;

    auto *pr = new PassRec{};
    pr->serial = ++s_serial;
    pr->compute = false;
    f->pass = pr;
    curry_val result;
    WindFrame *wf = push_wind(s_end_pass_prim);
    curry_val err = protected_apply1(av[1], serial_handle("gpu-pass", pr->serial), &result);
    pop_wind(wf);
    f->pass = nullptr;
    if (err) { delete pr; scm_raise_val(err); }
    ExnHandler h;
    SCM_PROTECT(h, execute_render_pass(f, pr, f->screen_rt, target, clear, dclear), err = h.exn);
    delete pr;
    if (err) scm_raise_val(err);
    if (!target) f->screen_drawn = true;
    return result;
}

/* Metal aborts the process on a dispatch over the device's limits instead
 * of reporting an error, so check against what QRhi says the device has. */
static void check_dispatch_limits(QRhi *rhi, const DispatchCmd &d) {
    int maxg = rhi->resourceLimit(QRhi::MaxThreadGroupsPerDimension);
    if (d.x > maxg || d.y > maxg || d.z > maxg)
        curry_error("gpu-dispatch!: group count over the device limit of %d per dimension", maxg);
    std::array<uint, 3> ls = d.pl->cs->shader.description().computeShaderLocalSize();
    int mx = rhi->resourceLimit(QRhi::MaxThreadGroupX), my = rhi->resourceLimit(QRhi::MaxThreadGroupY),
        mz = rhi->resourceLimit(QRhi::MaxThreadGroupZ), mt = rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup);
    if ((int)ls[0] > mx || (int)ls[1] > my || (int)ls[2] > mz || (qint64)ls[0] * ls[1] * ls[2] > mt)
        curry_error("gpu-dispatch!: the shader's local_size (%u, %u, %u) is over the device limit "
                    "(%d, %d, %d; %d threads in total)", ls[0], ls[1], ls[2], mx, my, mz, mt);
}

static void execute_compute_pass(FrameRec *f, PassRec *pr) {
    QRhi *rhi = f->rhi;
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    if (!u) curry_error("gpu-compute-pass!: out of resource update batches");
    batch_begin();
    ExnHandler h;
    curry_val err = 0;
    std::vector<QRhiComputePipeline *> pss(pr->dispatches.size());
    SCM_PROTECT(h, {
        for (size_t i = 0; i < pr->dispatches.size(); i++) {
            DispatchCmd &d = pr->dispatches[i];
            pss[i] = mat_compute(rhi, d.pl, u);
            mat_bindings(rhi, d.b, u);
            check_dispatch_limits(rhi, d);
        }
    }, err = h.exn);
    if (err) { batch_failed(); u->release(); scm_raise_val(err); }
    QRhiCommandBuffer *cb = f->cb;
    cb->beginComputePass(u);
    for (size_t i = 0; i < pr->dispatches.size(); i++) {
        DispatchCmd &d = pr->dispatches[i];
        cb->setComputePipeline(pss[i]);
        cb->setShaderResources(d.b->srb);
        cb->dispatch(d.x, d.y, d.z);
    }
    cb->endComputePass();
}

/* (gpu-compute-pass! frame proc) */
static curry_val fn_gpu_compute_pass(int, curry_val *av, void *) {
    const char *who = "gpu-compute-pass!";
    FrameRec *f = val_to_frame(av[0], who);
    if (!curry_is_procedure(av[1])) curry_error("%s: argument 2 must be a procedure", who);
    if (f->pass) curry_error("%s: passes cannot be nested", who);
    if (!f->rhi->isFeatureSupported(QRhi::Compute))
        curry_error("%s: the %s backend does not support compute", who, f->rhi->backendName());
    auto *pr = new PassRec{};
    pr->serial = ++s_serial;
    pr->compute = true;
    f->pass = pr;
    curry_val result;
    WindFrame *wf = push_wind(s_end_pass_prim);
    curry_val err = protected_apply1(av[1], serial_handle("gpu-pass", pr->serial), &result);
    pop_wind(wf);
    f->pass = nullptr;
    if (err) { delete pr; scm_raise_val(err); }
    ExnHandler h;
    SCM_PROTECT(h, execute_compute_pass(f, pr), err = h.exn);
    delete pr;
    if (err) scm_raise_val(err);
    return result;
}

static quint32 opt_count(const Opts &o, const char *key, quint32 dflt) {
    return (quint32)opt_uint(o, key, dflt, 0x7fffffff);
}

/* A uniform buffer holds one value per frame.  Recording a draw that uses
 * it counts as use, so update / draw / update / draw inside one pass
 * raises instead of silently giving both draws the last value. */
static void mark_uniforms_used(GpuBindings *b) {
    for (auto &e : b->entries)
        if (e.kind == BindKind::Uniform && s_frame) e.buf->used_in_frame = s_frame->serial;
}

/* Every vertex and instance the draw can fetch must lie inside its buffer:
 * Metal and OpenGL do not bounds-check vertex fetches, so an over-long
 * draw reads whatever memory follows the buffer.  For indexed draws the
 * largest index in the whole index buffer is used (conservative). */
static void check_draw_ranges(const DrawCmd &d, const char *who) {
    if (d.count == 0 || d.instances == 0) return;
    qint64 last_vertex = d.ibuf ? d.ibuf->largest_index() + d.vertex_offset
                                : (qint64)d.first + d.count - 1;
    qint64 last_instance = (qint64)d.first_instance + d.instances - 1;
    for (size_t i = 0; i < d.pl->vbindings.size(); i++) {
        const VInputBinding &b = d.pl->vbindings[i];
        if (b.attr_end == 0) continue;   /* slot with no attributes reads nothing */
        qint64 last = b.per_instance ? last_instance : last_vertex;
        qint64 need = last * (qint64)b.stride + b.attr_end;
        qint64 have = d.vbufs[i].first->data.size() - (qint64)d.vbufs[i].second;
        if (need > have)
            curry_error("%s: vertex buffer %zu holds %lld bytes but this draw reads %s %lld (needs %lld bytes)",
                        who, i, (long long)have, b.per_instance ? "instance" : "vertex",
                        (long long)last, (long long)need);
    }
}

/* (gpu-draw! pass pipeline count ['bindings b] ['vertices bufs] ['index ibuf]
 *            ['instances n] ['first n] ['first-instance n] ['vertex-offset n]
 *            ['viewport (x y w h)] ['scissor (x y w h)]) */
static curry_val fn_gpu_draw(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "bindings", "vertices", "index", "instances", "first",
        "first-instance", "vertex-offset", "viewport", "scissor", nullptr };
    const char *who = "gpu-draw!";
    PassRec *pr = val_to_pass(av[0], who, false);
    DrawCmd d{};
    d.pl = val_to_pipeline(av[1]);
    d.pl->check_live(who);
    intptr_t count = checked_fixnum(av[2], 3, who);
    if (count < 0) curry_error("%s: count must be non-negative", who);
    d.count = (quint32)count;
    Opts o = parse_opts(ac, av, 3, who, allowed);
    d.b = o.has("bindings") ? val_to_bindings(o.get("bindings")) : d.pl->layout;
    d.b->check_live(who);
    check_same_layout(d.pl->layout, d.b, who);
    if (d.b != d.pl->layout) {
        check_shader_resources(d.pl->vs, d.b, who);
        check_shader_resources(d.pl->fs, d.b, who);
    }
    if (o.has("vertices")) {
        curry_val vl = o.get("vertices");
        /* A handle is itself a pair (gpu-buffer . bytes): wrap a lone one. */
        if (!curry_is_nil(vl) && (!curry_is_pair(vl) || curry_is_symbol(curry_car(vl))))
            vl = curry_make_pair(vl, curry_nil());
        if (list_length(vl) < 0) curry_error("%s: 'vertices must be a buffer or list of buffers", who);
        for (curry_val p = vl; curry_is_pair(p); p = curry_cdr(p)) {
            GpuBuffer *b = val_to_buffer(curry_car(p));
            b->check_live(who);
            if (!(b->usage & QRhiBuffer::VertexBuffer)) curry_error("%s: vertex buffer was not made with usage 'vertex", who);
            d.vbufs.push_back({ b, 0 });
        }
    }
    if (d.vbufs.size() != d.pl->vbindings.size())
        curry_error("%s: pipeline expects %zu vertex buffer(s), got %zu", who, d.pl->vbindings.size(), d.vbufs.size());
    if (o.has("index")) {
        d.ibuf = val_to_buffer(o.get("index"));
        d.ibuf->check_live(who);
        if (!(d.ibuf->usage & QRhiBuffer::IndexBuffer)) curry_error("%s: 'index buffer was not made with usage 'index", who);
    }
    d.instances = opt_count(o, "instances", 1);
    d.first = opt_count(o, "first", 0);
    d.first_instance = opt_count(o, "first-instance", 0);
    d.vertex_offset = (qint32)opt_uint(o, "vertex-offset", 0, 0x7fffffff);
    if (d.ibuf && ((quint64)d.first + d.count) * 4 > (quint64)d.ibuf->data.size())
        curry_error("%s: %u indices from %u read past the end of the index buffer", who, d.count, d.first);
    for (const char *k : { "viewport", "scissor" }) {
        curry_val r = o.get(k);
        if (!r) continue;
        if (list_length(r) != 4) curry_error("%s: '%s must be (x y w h)", who, k);
        double q[4]; int i = 0;
        for (curry_val p = r; curry_is_pair(p); p = curry_cdr(p), i++) {
            if (!curry_is_number(curry_car(p))) curry_error("%s: '%s components must be numbers", who, k);
            q[i] = curry_number_to_double(curry_car(p));
            if (!std::isfinite(q[i]) || std::fabs(q[i]) > 65536.0)
                curry_error("%s: '%s components must be finite and within +-65536", who, k);
        }
        if (q[2] < 0 || q[3] < 0) curry_error("%s: '%s width and height must not be negative", who, k);
        if (k[0] == 'v') { d.has_vp = true; for (int j = 0; j < 4; j++) d.vp[j] = (float)q[j]; }
        else             { d.has_sc = true; for (int j = 0; j < 4; j++) d.sc[j] = (int)q[j]; }
    }
    check_draw_ranges(d, who);
    mark_uniforms_used(d.b);
    mark_uniforms_used(d.pl->layout);
    pr->draws.push_back(std::move(d));
    return curry_void();
}

/* (gpu-dispatch! pass compute-pipeline x [y z] ['bindings b]) */
static curry_val fn_gpu_dispatch(int ac, curry_val *av, void *) {
    static const char *const allowed[] = { "bindings", nullptr };
    const char *who = "gpu-dispatch!";
    PassRec *pr = val_to_pass(av[0], who, true);
    DispatchCmd d{};
    d.pl = val_to_compute(av[1]);
    d.pl->check_live(who);
    int nums = 0;
    intptr_t g[3] = { 1, 1, 1 };
    for (int i = 2; i < ac && i < 5 && curry_is_fixnum(av[i]); i++) g[nums++] = curry_fixnum(av[i]);
    if (nums == 0) curry_error("%s: need at least the x group count", who);
    for (int i = 0; i < 3; i++)
        if (g[i] < 1 || g[i] > 65535) curry_error("%s: group counts must be in [1, 65535]", who);
    Opts o = parse_opts(ac, av, 2 + nums, who, allowed);
    d.b = o.has("bindings") ? val_to_bindings(o.get("bindings")) : d.pl->layout;
    d.b->check_live(who);
    check_same_layout(d.pl->layout, d.b, who);
    if (d.b != d.pl->layout) check_shader_resources(d.pl->cs, d.b, who);
    d.x = (int)g[0]; d.y = (int)g[1]; d.z = (int)g[2];
    mark_uniforms_used(d.b);
    mark_uniforms_used(d.pl->layout);
    pr->dispatches.push_back(d);
    return curry_void();
}

/* ---- readback ------------------------------------------------------------ */

/* Queue a readback in the current frame (outside any pass).  For device
 * frames the proc runs after the frame ends (gpu-run!); for canvas frames
 * it runs when Qt reports the GPU work finished (a later frame). */
static void queue_readback(FrameRec *f, curry_val proc, bool is_tex, GpuBuffer *b, GpuTexture *t) {
    QRhi *rhi = f->rhi;
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    if (!u) curry_error("gpu: out of resource update batches");
    batch_begin();
    ExnHandler h;
    curry_val err = 0;
    SCM_PROTECT(h, { if (is_tex) mat_texture(rhi, t, u); else mat_buffer(rhi, b, u); }, err = h.exn);
    if (err) { batch_failed(); u->release(); scm_raise_val(err); }
    auto *rb = new Readback{};
    rb->proc_slot = new curry_val(proc);
    gc_register_root(rb->proc_slot);
    if (f->device_frame) {
        f->device_readbacks.push_back(rb);
    } else {
        rb->result.completed = [rb]() {
            /* Called from inside QRhi's own frame processing: do not run
             * Scheme here (it could release resources, start a nested
             * frame, or escape through QRhi).  Run it on a later
             * event-loop turn; QRhi may also still touch rb->result after
             * this returns. */
            QMetaObject::invokeMethod(qApp, [rb]() {
                curry_val bv = bytes_to_bv(rb->result.data);
                curry_val p = *rb->proc_slot;
                gc_unregister_root(rb->proc_slot);
                delete rb->proc_slot;
                delete rb;
                SCHEME_CALL(curry_apply(p, 1, &bv));
            }, Qt::QueuedConnection);
        };
    }
    if (is_tex) u->readBackTexture(QRhiReadbackDescription(t->tex), &rb->result);
    else        u->readBackBuffer(b->buf, 0, (quint32)b->data.size(), &rb->result);
    f->cb->resourceUpdate(u);
}

static curry_val fn_gpu_read_buffer_async(int, curry_val *av, void *) {
    const char *who = "gpu-read-buffer!";
    FrameRec *f = val_to_frame(av[0], who);
    if (f->pass) curry_error("%s: cannot read back inside a pass", who);
    GpuBuffer *b = val_to_buffer(av[1]);
    b->check_live(who);
    if (!curry_is_procedure(av[2])) curry_error("%s: argument 3 must be a procedure", who);
    queue_readback(f, av[2], false, b, nullptr);
    return curry_void();
}

static curry_val fn_gpu_read_texture_async(int, curry_val *av, void *) {
    const char *who = "gpu-read-texture!";
    FrameRec *f = val_to_frame(av[0], who);
    if (f->pass) curry_error("%s: cannot read back inside a pass", who);
    GpuTexture *t = val_to_texture(av[1]);
    t->check_live(who);
    if (t->d) curry_error("%s: 3D texture readback is not supported", who);
    if (!curry_is_procedure(av[2])) curry_error("%s: argument 3 must be a procedure", who);
    queue_readback(f, av[2], true, nullptr, t);
    return curry_void();
}

/* Synchronous device readback: its own offscreen frame. */
static QByteArray device_readback(QRhi *rhi, bool is_tex, GpuBuffer *b, GpuTexture *t, const char *who) {
    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) curry_error("%s: could not begin a frame", who);
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    if (!u) { rhi->endOffscreenFrame(); curry_error("%s: out of resource update batches", who); }
    batch_begin();
    ExnHandler h;
    curry_val err = 0;
    SCM_PROTECT(h, { if (is_tex) mat_texture(rhi, t, u); else mat_buffer(rhi, b, u); }, err = h.exn);
    if (err) { batch_failed(); u->release(); rhi->endOffscreenFrame(); scm_raise_val(err); }
    QRhiReadbackResult res;
    if (is_tex) u->readBackTexture(QRhiReadbackDescription(t->tex), &res);
    else        u->readBackBuffer(b->buf, 0, (quint32)b->data.size(), &res);
    cb->resourceUpdate(u);
    rhi->endOffscreenFrame();
    return res.data;
}

static curry_val fn_gpu_read_buffer(int, curry_val *av, void *) {
    const char *who = "gpu-read-buffer";
    forbid_during_frame(who);
    QRhi *rhi = device_rhi(val_to_device(av[0]), who);
    GpuBuffer *b = val_to_buffer(av[1]);
    b->check_live(who);
    QByteArray *d = new QByteArray(device_readback(rhi, false, b, nullptr, who));
    curry_val bv = bytes_to_bv(*d);
    delete d;
    return bv;
}

static curry_val fn_gpu_read_texture(int, curry_val *av, void *) {
    const char *who = "gpu-read-texture";
    forbid_during_frame(who);
    QRhi *rhi = device_rhi(val_to_device(av[0]), who);
    GpuTexture *t = val_to_texture(av[1]);
    t->check_live(who);
    if (t->d) curry_error("%s: 3D texture readback is not supported", who);
    QByteArray *d = new QByteArray(device_readback(rhi, true, nullptr, t, who));
    curry_val bv = bytes_to_bv(*d);
    delete d;
    return bv;
}

/* (gpu-save-texture-png device tex path) — rgba8 textures only; rows are
 * flipped on backends whose framebuffer origin is bottom-left (OpenGL). */
static curry_val fn_gpu_save_texture_png(int, curry_val *av, void *) {
    const char *who = "gpu-save-texture-png";
    forbid_during_frame(who);
    QRhi *rhi = device_rhi(val_to_device(av[0]), who);
    GpuTexture *t = val_to_texture(av[1]);
    t->check_live(who);
    const char *path = checked_string(av[2], 3, who);
    if (t->d || t->fi->fmt != QRhiTexture::RGBA8) curry_error("%s: only 2D rgba8 textures can be saved", who);
    QByteArray *d = new QByteArray(device_readback(rhi, true, nullptr, t, who));
    bool ok = false;
    if (d->size() == t->byte_size()) {
        QImage img((const uchar *)d->constData(), t->w, t->h, t->w * 4, QImage::Format_RGBA8888);
        if (rhi->isYUpInFramebuffer()) img = img.flipped(Qt::Vertical);
        ok = img.save(QString::fromUtf8(path), "PNG");
    }
    delete d;
    return curry_make_bool(ok);
}

/* ---- byte packing helpers ------------------------------------------------ */

static curry_val pack_bytes(curry_val v, Elem e, const char *who) {
    if (curry_is_bytevector(v)) curry_error("%s: data must be numbers, not a bytevector", who);
    QByteArray *d = new QByteArray();
    ExnHandler h;
    curry_val err = 0;
    SCM_PROTECT(h, flatten_numbers(*d, v, e, who), err = h.exn);
    if (err) { delete d; scm_raise_val(err); }
    curry_val bv = bytes_to_bv(*d);
    delete d;
    return bv;
}
static curry_val fn_gpu_f32_bytes(int, curry_val *av, void *) { return pack_bytes(av[0], Elem::F32, "gpu-f32-bytes"); }
static curry_val fn_gpu_u32_bytes(int, curry_val *av, void *) { return pack_bytes(av[0], Elem::U32, "gpu-u32-bytes"); }
static curry_val fn_gpu_i32_bytes(int, curry_val *av, void *) { return pack_bytes(av[0], Elem::I32, "gpu-i32-bytes"); }

static curry_val fn_gpu_bytes_to_f32(int, curry_val *av, void *) {
    if (!curry_is_bytevector(av[0])) curry_error("gpu-bytes->f32: argument must be a bytevector");
    uint32_t n = curry_bytevector_length(av[0]) / 4;
    const uint8_t *p = curry_bytevector_data(av[0]);
    F64Vec *fv = CURRY_NEW_FLEX_ATOM(F64Vec, n);
    fv->hdr.type = T_F64VEC; fv->hdr.flags = 0; fv->len = n;
    for (uint32_t i = 0; i < n; i++) { float f; memcpy(&f, p + 4 * i, 4); fv->data[i] = f; }
    return vptr(fv);
}

static curry_val ints_from_bytes(curry_val bvv, bool is_signed, const char *who) {
    if (!curry_is_bytevector(bvv)) curry_error("%s: argument must be a bytevector", who);
    uint32_t n = curry_bytevector_length(bvv) / 4;
    const uint8_t *p = curry_bytevector_data(bvv);
    curry_val v = curry_make_vector(n, curry_make_fixnum(0));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t u; memcpy(&u, p + 4 * i, 4);
        curry_vector_set(v, i, curry_make_fixnum(is_signed ? (intptr_t)(int32_t)u : (intptr_t)u));
    }
    return v;
}
static curry_val fn_gpu_bytes_to_u32(int, curry_val *av, void *) { return ints_from_bytes(av[0], false, "gpu-bytes->u32"); }
static curry_val fn_gpu_bytes_to_i32(int, curry_val *av, void *) { return ints_from_bytes(av[0], true, "gpu-bytes->i32"); }

/* ---- std140 / std430 block packing ------------------------------------------
 *
 * (gpu-std140 fields) / (gpu-std430 fields) -> bytevector laid out the way
 * GLSL lays out a uniform (std140) or storage (std430) block, so Scheme
 * code never computes padding by hand.  fields:
 *   (type value)                 type: float int uint vec2..4 ivec2..4
 *   (array type (value ...))           uvec2..4 mat2 mat3 mat4
 * Matrices are column-major sequences (16 numbers for mat4), the same
 * convention as gpu-clip-space-matrix and (curry gpu-math).
 */

struct BlockType { const char *name; int comps; int cols; Elem elem; };
static const BlockType k_block_types[] = {
    { "float", 1, 1, Elem::F32 }, { "int", 1, 1, Elem::I32 }, { "uint", 1, 1, Elem::U32 },
    { "vec2", 2, 1, Elem::F32 },  { "vec3", 3, 1, Elem::F32 }, { "vec4", 4, 1, Elem::F32 },
    { "ivec2", 2, 1, Elem::I32 }, { "ivec3", 3, 1, Elem::I32 }, { "ivec4", 4, 1, Elem::I32 },
    { "uvec2", 2, 1, Elem::U32 }, { "uvec3", 3, 1, Elem::U32 }, { "uvec4", 4, 1, Elem::U32 },
    { "mat2", 2, 2, Elem::F32 },  { "mat3", 3, 3, Elem::F32 }, { "mat4", 4, 4, Elem::F32 },
};

static int vec_align(int comps) { return comps == 1 ? 4 : comps == 2 ? 8 : 16; }
static qsizetype round_up(qsizetype x, qsizetype a) { return (x + a - 1) / a * a; }

static void pack_block_into(QByteArray &out, curry_val fields, bool std430, const char *who) {
    if (list_length(fields) < 0) curry_error("%s: argument must be a list of fields", who);
    qsizetype max_align = std430 ? 4 : 16;
    for (curry_val p = fields; curry_is_pair(p); p = curry_cdr(p)) {
        curry_val f = curry_car(p);
        long fl = list_length(f);
        bool is_array = fl == 3 && sym_is(curry_car(f), "array");
        if (!(fl == 2 || is_array) || !curry_is_symbol(is_array ? curry_car(curry_cdr(f)) : curry_car(f)))
            curry_error("%s: fields are (type value) or (array type (value ...))", who);
        const char *tn = curry_symbol(is_array ? curry_car(curry_cdr(f)) : curry_car(f));
        curry_val value = is_array ? curry_car(curry_cdr(curry_cdr(f))) : curry_car(curry_cdr(f));
        const BlockType *t = nullptr;
        for (auto &bt : k_block_types) if (strcmp(bt.name, tn) == 0) t = &bt;
        if (!t) curry_error("%s: unknown type '%s", who, tn);

        /* Column layout: a matrix is an array of `cols` column vectors. */
        qsizetype col_align = vec_align(t->comps);
        qsizetype col_stride = t->cols > 1 ? (std430 ? col_align : 16) : t->comps * 4;
        qsizetype elem_size = t->cols > 1 ? col_stride * t->cols : t->comps * 4;
        qsizetype align = t->cols > 1 ? (std430 ? col_align : 16) : col_align;
        long count = 1;
        if (is_array) {
            count = list_length(value);
            if (count <= 0) curry_error("%s: array of %s must be a non-empty list", who, tn);
            if (!std430 && align < 16) align = 16;   /* std140: array elements are vec4-aligned */
        }
        qsizetype stride = is_array ? round_up(elem_size, align) : elem_size;
        if (align > max_align) max_align = align;

        QByteArray vals;
        flatten_numbers(vals, value, t->elem, who);
        long want = count * t->comps * t->cols;
        if (vals.size() != want * 4)
            curry_error("%s: %s%s needs %ld numbers, got %lld", who, is_array ? "array of " : "", tn,
                        want, (long long)(vals.size() / 4));
        qsizetype base = round_up(out.size(), align);
        out.append(QByteArray(base - out.size() + stride * count, '\0'));
        const char *src = vals.constData();
        for (long e = 0; e < count; e++)
            for (int c = 0; c < t->cols; c++) {
                memcpy(out.data() + base + e * stride + c * col_stride, src, (size_t)t->comps * 4);
                src += t->comps * 4;
            }
    }
    out.append(QByteArray(round_up(out.size(), max_align) - out.size(), '\0'));
}

static curry_val pack_block(curry_val fields, bool std430, const char *who) {
    QByteArray *d = new QByteArray();
    ExnHandler h;
    curry_val err = 0;
    SCM_PROTECT(h, pack_block_into(*d, fields, std430, who), err = h.exn);
    if (err) { delete d; scm_raise_val(err); }
    if (d->isEmpty()) { delete d; curry_error("%s: block is empty", who); }
    curry_val bv = bytes_to_bv(*d);
    delete d;
    return bv;
}
static curry_val fn_gpu_std140(int, curry_val *av, void *) { return pack_block(av[0], false, "gpu-std140"); }
static curry_val fn_gpu_std430(int, curry_val *av, void *) { return pack_block(av[0], true, "gpu-std430"); }

/* =========================================================================
 * Registration
 * ========================================================================= */

/* Every gpu-* procedure that touches GPU state runs on the Qt application
 * thread only: the frame state is global, QRhi objects are not
 * thread-safe, and a canvas renders on that thread anyway.  Registered
 * through a trampoline that checks the thread before calling the real
 * function (review of #347: two actors running gpu-run! raced). */
struct GpuFnEntry { CurryFn fn; const char *name; };

static curry_val main_thread_trampoline(int ac, curry_val *av, void *ud) {
    const GpuFnEntry *e = (const GpuFnEntry *)ud;
    if (!qApp || QThread::currentThread() != qApp->thread())
        curry_error("%s: gpu-* procedures can only be used from the main thread (not from an actor)", e->name);
    return e->fn(ac, av, nullptr);
}

static void gpu_def(CurryVM *vm, const char *name, CurryFn fn, int min_args, int max_args) {
    curry_define_fn(vm, name, main_thread_trampoline, min_args, max_args, new GpuFnEntry{ fn, name });
}

void qt6_gpu_register(CurryVM *vm) {
    s_noop_prim = make_prim("gpu-wind-noop", fn_noop);
    s_end_pass_prim = make_prim("gpu-end-pass-on-escape", fn_end_pass_on_escape);
    s_end_device_frame_prim = make_prim("gpu-end-frame-on-escape", fn_end_device_frame_on_escape);
    s_end_canvas_frame_prim = make_prim("gpu-end-canvas-frame-on-escape", fn_end_canvas_frame_on_escape);
    gc_register_root(&s_end_canvas_frame_prim);
    gc_register_root(&s_noop_prim);
    gc_register_root(&s_end_pass_prim);
    gc_register_root(&s_end_device_frame_prim);

    /* Devices and canvases */
    gpu_def(vm, "make-gpu-device", fn_make_gpu_device, 0, 1);
    gpu_def(vm, "gpu-device-destroy!", fn_gpu_device_destroy, 1, 1);
    gpu_def(vm, "make-gpu-canvas", fn_make_gpu_canvas, 0, -1);
    gpu_def(vm, "gpu-canvas-on-render!", fn_gpu_canvas_on_render, 2, 2);
    gpu_def(vm, "gpu-canvas-on-key!", fn_gpu_canvas_on_key, 2, 2);
    gpu_def(vm, "gpu-canvas-on-key-up!", fn_gpu_canvas_on_key_up, 2, 2);
    gpu_def(vm, "gpu-canvas-on-mouse!", fn_gpu_canvas_on_mouse, 2, 2);
    gpu_def(vm, "gpu-canvas-on-scroll!", fn_gpu_canvas_on_scroll, 2, 2);
    gpu_def(vm, "gpu-canvas-on-resize!", fn_gpu_canvas_on_resize, 2, 2);
    gpu_def(vm, "gpu-canvas-on-grab-move!", fn_gpu_canvas_on_grab_move, 2, 2);
    gpu_def(vm, "gpu-canvas-grab-mouse!",   fn_gpu_canvas_grab_mouse,   1, 1);
    gpu_def(vm, "gpu-canvas-release-mouse!", fn_gpu_canvas_release_mouse, 1, 1);
    gpu_def(vm, "gpu-canvas-mouse-grabbed?", fn_gpu_canvas_mouse_grabbed_p, 1, 1);
    gpu_def(vm, "gpu-canvas-focus!",        fn_gpu_canvas_focus,        1, 1);
    gpu_def(vm, "gpu-canvas-redraw!", fn_gpu_canvas_redraw, 1, 1);
    gpu_def(vm, "gpu-canvas-save-png!", fn_gpu_canvas_save_png, 2, 2);

    /* Queries */
    gpu_def(vm, "gpu-backend", fn_gpu_backend, 1, 1);
    gpu_def(vm, "gpu-device-name", fn_gpu_device_name, 1, 1);
    gpu_def(vm, "gpu-feature?", fn_gpu_feature_p, 2, 2);
    curry_define_fn(vm, "gpu-features",             fn_gpu_features,             0, 0, NULL);
    gpu_def(vm, "gpu-limit", fn_gpu_limit, 2, 2);
    gpu_def(vm, "gpu-clip-space-matrix", fn_gpu_clip_space_matrix, 1, 1);
    gpu_def(vm, "gpu-framebuffer-y-up?", fn_gpu_y_up_p, 1, 1);

    /* Resources */
    gpu_def(vm, "make-gpu-buffer", fn_make_gpu_buffer, 2, 2);
    gpu_def(vm, "gpu-buffer-update!", fn_gpu_buffer_update, 2, 3);
    gpu_def(vm, "gpu-buffer-size", fn_gpu_buffer_size, 1, 1);
    gpu_def(vm, "make-gpu-texture", fn_make_gpu_texture, 2, -1);
    gpu_def(vm, "gpu-texture-update!", fn_gpu_texture_update, 2, 2);
    gpu_def(vm, "gpu-texture-size", fn_gpu_texture_size, 1, 1);
    gpu_def(vm, "make-gpu-sampler", fn_make_gpu_sampler, 0, -1);
    gpu_def(vm, "make-gpu-shader", fn_make_gpu_shader, 2, 2);
    gpu_def(vm, "make-gpu-bindings", fn_make_gpu_bindings, 1, 1);
    gpu_def(vm, "make-gpu-pipeline", fn_make_gpu_pipeline, 0, -1);
    gpu_def(vm, "make-gpu-compute-pipeline", fn_make_gpu_compute_pipeline, 1, 2);
    gpu_def(vm, "gpu-release!", fn_gpu_release, 1, 1);

    /* Frames, passes, commands */
    gpu_def(vm, "gpu-run!", fn_gpu_run, 2, 2);
    gpu_def(vm, "gpu-render-pass!", fn_gpu_render_pass, 2, -1);
    gpu_def(vm, "gpu-compute-pass!", fn_gpu_compute_pass, 2, 2);
    gpu_def(vm, "gpu-draw!", fn_gpu_draw, 3, -1);
    gpu_def(vm, "gpu-dispatch!", fn_gpu_dispatch, 3, -1);

    /* Readback */
    gpu_def(vm, "gpu-read-buffer", fn_gpu_read_buffer, 2, 2);
    gpu_def(vm, "gpu-read-texture", fn_gpu_read_texture, 2, 2);
    gpu_def(vm, "gpu-read-buffer!", fn_gpu_read_buffer_async, 3, 3);
    gpu_def(vm, "gpu-read-texture!", fn_gpu_read_texture_async, 3, 3);
    gpu_def(vm, "gpu-save-texture-png", fn_gpu_save_texture_png, 3, 3);

    /* Byte packing */
    curry_define_fn(vm, "gpu-f32-bytes",            fn_gpu_f32_bytes,            1, 1, NULL);
    curry_define_fn(vm, "gpu-u32-bytes",            fn_gpu_u32_bytes,            1, 1, NULL);
    curry_define_fn(vm, "gpu-i32-bytes",            fn_gpu_i32_bytes,            1, 1, NULL);
    curry_define_fn(vm, "gpu-bytes->f32",           fn_gpu_bytes_to_f32,         1, 1, NULL);
    curry_define_fn(vm, "gpu-bytes->u32",           fn_gpu_bytes_to_u32,         1, 1, NULL);
    curry_define_fn(vm, "gpu-bytes->i32",           fn_gpu_bytes_to_i32,         1, 1, NULL);
    curry_define_fn(vm, "gpu-std140",               fn_gpu_std140,               1, 1, NULL);
    curry_define_fn(vm, "gpu-std430",               fn_gpu_std430,               1, 1, NULL);
}
