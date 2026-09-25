#include "interpreter.hpp"

/* numeric.h (pulled in transitively below, via object.h/compiler.h) drags
 * in <gmp.h>, which declares C++-only operator>> overloads for mpz_ptr/
 * mpq_ptr/mpf_ptr under #ifdef __cplusplus. If that first happens *inside*
 * the extern "C" block below, those three overloads all get C linkage
 * instead of C++ linkage -- C has no overloading, so they collide as
 * "conflicting types for operator>>". Including gmp.h here, in ordinary
 * C++ mode, first satisfies its own include guard so the later transitive
 * include is a no-op and never re-declares anything under extern "C". */
#include <gmp.h>

extern "C" {
#include "gc.h"
#include "value.h"
#include "object.h"
#include "symbol.h"
#include "env.h"
#include "port.h"
#include "reader.h"
#include "compiler.h"
#include "vm.h"
#include "eval.h"
#include "condition.h"
#include "runtime_init.h"
#include "nesting_depth.h"
#include "interrupt.h"
#include "version.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace curry_jupyter {

namespace {

std::string curry_string_to_std(val_t v) {
    if (!vis_string(v)) return {};
    String *s = as_str(v);
    return std::string(str_data(s), s->len);
}

/* Render a value the same way the REPL's print_result() does (write
 * syntax, not display) by writing it into a fresh string port. */
std::string render_value(val_t v) {
    val_t port = port_open_output_string();
    scm_write_shared(v, port);
    std::string text = curry_string_to_std(port_get_output_string(port));
    port_close(port);
    return text;
}

/* Render a raised condition/error the same way print_scheme_error()
 * does, but into a string instead of stderr -- used for both ename
 * (short) and the traceback lines the reply needs. */
struct RenderedError {
    std::string ename;
    std::string evalue;
    std::vector<std::string> traceback;
};

RenderedError render_error(val_t exn) {
    RenderedError r;
    val_t code = vis_error(exn) ? as_err(exn)->code : V_FALSE;
    r.ename = vis_symbol(code) ? std::string(sym_cstr(code)) : "error";
    val_t message = vis_error(exn) ? as_err(exn)->message : exn;
    if (vis_condition(exn)) {
        Condition *c = as_condition(exn);
        if (vis_symbol(c->type_sym)) r.ename = sym_cstr(c->type_sym);
        message = c->message;
    }
    if (vis_string(message)) {
        r.evalue = curry_string_to_std(message);
    } else {
        /* render_value calls scm_write_shared, which can itself raise
         * (e.g. check_c_stack_depth's stack-overflow guard firing on a
         * sufficiently deep but perfectly ordinary, non-circular
         * structure -- src/runtime.c's own comment on that guard). This
         * function runs from inside execute_request_impl's SCM_PROTECT
         * on_exn block, where current_handler has already been popped
         * back to whatever was installed before *this* cell's
         * protection (NULL, for this kernel's outermost call chain) --
         * SCM_PROTECT's macro expansion pops current_handler before
         * running on_exn, not after. Without its own protection here, a
         * pathological (still finite, non-circular) exn value would hit
         * scm_raise_val's no-current_handler fallback in
         * src/runtime.c and abort() the whole kernel process while just
         * trying to report an unrelated error. Same reasoning and same
         * fix as the render_value(last_value) call in
         * execute_request_impl. */
        ExnHandler h2;
        SCM_PROTECT(h2, {
            r.evalue = render_value(exn);
        }, {
            r.evalue = "<error value too deep or otherwise unprintable>";
            vm_reset();
        });
    }

    /* JupyterLab renders only the traceback list, not evalue separately.
     * Prepend the error message as the first traceback line so it's always
     * visible — the same convention used by IPython and other kernels. */
    r.traceback.push_back(r.ename + ": " + r.evalue);

    val_t bt = vis_error(exn) ? as_err(exn)->backtrace : V_NIL;
    for (val_t f = bt; vis_pair(f); f = vcdr(f)) {
        val_t frame = vcar(f);
        val_t name  = vcar(frame);
        val_t file  = vcadr(frame);
        val_t line  = vcaddr(frame);
        std::ostringstream os;
        os << "  at " << (vis_string(name) ? curry_string_to_std(name) : "?");
        if (vis_string(file)) {
            os << " (" << curry_string_to_std(file);
            if (vis_fixnum(line)) os << ":" << (long)vunfix(line);
            os << ")";
        } else if (vis_fixnum(line)) {
            os << " (line " << (long)vunfix(line) << ")";
        }
        r.traceback.push_back(os.str());
    }
    return r;
}

/* ---- (jupyter-display-file path) ----
 *
 * Jupyter-only builtin (registered into GLOBAL_ENV below, not compiled
 * into curry_core -- the plain `curry` REPL/CLI has no use for it and
 * doesn't get it). Lets Scheme code -- e.g. after plplot writes a PNG
 * with (curry plplot) -- ask the kernel to publish that file as a
 * display_data message, so it renders inline in the notebook instead of
 * only existing as a file on disk. See docs/reference/jupyter-kernel.md.
 */

std::string base64_encode(const std::string &data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8) | uint8_t(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += table[n & 0x3F];
        i += 3;
    }
    size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t n = uint8_t(data[i]) << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t n = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

/* Extension -> (MIME type, needs base64). SVG is text, not base64 --
 * per the Jupyter messaging spec, only genuinely binary MIME types are
 * base64-encoded in a display_data payload. A plain string literal (no
 * ownership) -- see the note on FileLoadResult below for why. */
const char *mime_for_extension(const char *path, bool &binary) {
    size_t plen = strlen(path);
    auto ends_with = [&](const char *suffix) {
        size_t n = strlen(suffix);
        return plen >= n && strcmp(path + plen - n, suffix) == 0;
    };
    if (ends_with(".png"))  { binary = true;  return "image/png"; }
    if (ends_with(".jpg") || ends_with(".jpeg")) { binary = true; return "image/jpeg"; }
    if (ends_with(".svg"))  { binary = false; return "image/svg+xml"; }
    return nullptr;
}

/* Plain-old-data result: deliberately holds no std::string/nl::json (no
 * type with a non-trivial destructor). scm_raise()'s longjmp skips
 * destructors of any C++ object still on the stack at the raise site --
 * UB per the standard regardless of whether that particular destructor
 * would have had an observable side effect. jupyter_display_file_prim
 * below only ever calls scm_raise() while nothing but POD locals (this
 * struct, a fixed-size char[] path buffer) are in scope; std::string/
 * nl::json are constructed only in the guaranteed-no-more-raises tail
 * after a successful load. */
struct FileLoadResult {
    enum { OK, BAD_EXTENSION, READ_FAILED } status;
    const char *mime; /* string literal from mime_for_extension; only valid when status == OK */
    bool binary;
    char *data;       /* malloc'd; caller must free() when non-null */
    size_t len;
};

FileLoadResult load_display_file(const char *path) {
    FileLoadResult r{};
    bool binary;
    const char *mime = mime_for_extension(path, binary);
    if (!mime) { r.status = FileLoadResult::BAD_EXTENSION; return r; }

    FILE *f = fopen(path, "rb");
    if (!f) { r.status = FileLoadResult::READ_FAILED; return r; }
    size_t cap = 65536, len = 0;
    char *buf = static_cast<char *>(malloc(cap));
    if (!buf) { fclose(f); r.status = FileLoadResult::READ_FAILED; return r; }
    char chunk[65536];
    size_t n;
    bool alloc_failed = false;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (len + n > cap) {
            size_t newcap = (len + n) * 2;
            char *grown = static_cast<char *>(realloc(buf, newcap));
            /* realloc leaves the original block untouched (and still
             * owned by `buf`) when it fails -- must free it via the
             * original pointer, not `grown` (NULL), or it leaks. */
            if (!grown) { alloc_failed = true; break; }
            buf = grown;
            cap = newcap;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    bool read_ok = !ferror(f);
    fclose(f);
    if (alloc_failed || !read_ok) { free(buf); r.status = FileLoadResult::READ_FAILED; return r; }

    r.status = FileLoadResult::OK;
    r.mime = mime; r.binary = binary; r.data = buf; r.len = len;
    return r;
}

/* Display ids seen so far in this kernel process -- the first
 * jupyter-display-file call for a given id publishes a normal display_data
 * (so the frontend has something to attach the id to); every later call
 * with the same id publishes update_display_data instead, which the
 * frontend renders by replacing that existing output in place. This is
 * the same two-message mechanism IPython's display(..., display_id=)/
 * update_display(...) uses under the hood -- it's what backs matplotlib's
 * notebook animation support in Python, and gives curry the same
 * capability without any GIF encoding or extra frontend machinery. A
 * plain process-lifetime set (not tied to execution_counter/cell) is
 * intentional: an animation loop spanning many top-level forms within one
 * cell, or reused across cells, both still update the same output.
 *
 * curry's actor system (src/actors.h) runs spawned Scheme code on real
 * detached pthreads, and jupyter-display-file is a plain GLOBAL_ENV
 * binding an actor can call just as freely as the main REPL thread --
 * concurrent unsynchronized insert() into an unordered_set is a data
 * race (UB, not just a wrong first_time answer), so this needs its own
 * lock even though the kernel's own execute_request_impl is otherwise
 * single-threaded. */
std::mutex seen_display_ids_mutex;
std::unordered_set<std::string> seen_display_ids;

val_t jupyter_display_file_prim(int argc, val_t *argv, void *ud) {
    if (!vis_string(argv[0]))
        scm_raise(V_FALSE, "jupyter-display-file: expected a path string");

    /* Fixed-size stack buffers, not std::string, for the same reason
     * FileLoadResult is POD -- these need to stay valid (and destructor-
     * free) across the scm_raise() calls below. */
    String *s = as_str(argv[0]);
    char path[4096];
    if (s->len >= sizeof(path))
        scm_raise(V_FALSE, "jupyter-display-file: path too long (max %zu bytes)", sizeof(path) - 1);
    memcpy(path, str_data(s), s->len);
    path[s->len] = '\0';

    bool has_id = argc >= 2;
    char id[256];
    if (has_id) {
        const char *id_src;
        uint32_t id_len;
        if (vis_string(argv[1])) {
            String *is = as_str(argv[1]);
            id_src = str_data(is);
            id_len = is->len;
        } else if (vis_symbol(argv[1])) {
            id_src = sym_cstr(argv[1]);
            id_len = sym_len(argv[1]);
        } else {
            scm_raise(V_FALSE, "jupyter-display-file: display-id must be a string or symbol");
            return V_VOID; /* unreachable: scm_raise() longjmps */
        }
        if (id_len >= sizeof(id))
            scm_raise(V_FALSE, "jupyter-display-file: display-id too long (max %zu bytes)", sizeof(id) - 1);
        memcpy(id, id_src, id_len);
        id[id_len] = '\0';
    }

    FileLoadResult r = load_display_file(path);
    if (r.status == FileLoadResult::BAD_EXTENSION)
        scm_raise(V_FALSE, "jupyter-display-file: unsupported file extension: %s", path);
    if (r.status == FileLoadResult::READ_FAILED)
        scm_raise(V_FALSE, "jupyter-display-file: cannot read file: %s", path);

    /* Success only from here on -- no further scm_raise() calls, so
     * std::string/nl::json are safe to use for the rest of this call. */
    std::string content(r.data, r.len);
    free(r.data);

    nl::json data;
    data[r.mime] = r.binary ? base64_encode(content) : content;
    data["text/plain"] = std::string("<") + r.mime + ": " + path + ">";

    auto *interp = static_cast<curry_interpreter *>(ud);
    if (!has_id) {
        interp->display_data(data, nl::json::object(), nl::json::object());
        return V_VOID;
    }

    nl::json transient;
    transient["display_id"] = id;
    bool first_time;
    {
        std::lock_guard<std::mutex> lock(seen_display_ids_mutex);
        first_time = seen_display_ids.insert(id).second;
    }
    if (first_time) {
        interp->display_data(data, nl::json::object(), transient);
    } else {
        interp->update_display_data(data, nl::json::object(), transient);
    }
    return V_VOID;
}

} // namespace

void curry_interpreter::configure_impl() {
    curry_runtime_init();

    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type  = T_PRIMITIVE; p->hdr.flags = 0;
    p->name      = "jupyter-display-file";
    p->min_args  = 1; p->max_args = 2;
    p->fn        = jupyter_display_file_prim;
    p->ud        = this;
    env_define(GLOBAL_ENV, sym_intern_cstr("jupyter-display-file"), vptr(p));
}

void curry_interpreter::execute_request_impl(xeus::xinterpreter::send_reply_callback cb,
                                              int execution_counter,
                                              const std::string &code,
                                              xeus::execute_request_config /*config*/,
                                              nl::json /*user_expressions*/) {
    /* configure_impl() (which calls curry_runtime_init() -> gc_init() ->
     * GC_INIT()) registers whichever thread calls it as Boehm's "main" GC
     * thread implicitly. If xeus-zmq ever dispatches shell-channel
     * callbacks on a different thread than the one that ran configure_impl,
     * this thread's C stack wouldn't be scanned and every val_t local below
     * would be invisible to the collector -- silent heap corruption.
     * gc_register_thread() is a documented no-op if already registered
     * (GC_register_my_thread returns GC_DUPLICATE), so calling it
     * defensively here costs nothing either way. */
    gc_register_thread();

    val_t in_port = port_open_input_string(code.c_str(), (uint32_t)code.size());

    val_t last_value = V_VOID;
    bool  have_error  = false;
    RenderedError err;

    for (;;) {
        ExnHandler h;
        val_t out_port = port_open_output_string();
        val_t saved_stdout = PORT_STDOUT;
        PORT_STDOUT = out_port;

        /* scm_read() itself can raise (e.g. EOF mid-form on unbalanced
         * parens) -- it must be inside the same protected block as
         * compile+run, not called before SCM_PROTECT is installed.
         * Reading it unprotected was a real bug: a single malformed
         * cell raised with no ExnHandler in scope, hit
         * scm_raise_val()'s "no current_handler" fallback in
         * src/runtime.c, and abort()ed the whole kernel process. */
        bool raised = false, at_eof = false;
        SCM_PROTECT(h, {
            val_t form = scm_read(in_port);
            if (vis_eof(form)) {
                at_eof = true;
            } else {
                val_t cl = compiler_compile(form);
                last_value = vm_run(as_bcclosure(cl), 0);
            }
        }, {
            raised = true;
            err = render_error(h.exn);
        });

        PORT_STDOUT = saved_stdout;
        std::string streamed = curry_string_to_std(port_get_output_string(out_port));
        port_close(out_port);
        if (!streamed.empty()) publish_stream("stdout", streamed);

        if (raised) {
            have_error = true;
            vm_reset();
            break;
        }
        if (at_eof) break;
    }
    port_close(in_port);

    if (have_error) {
        publish_execution_error(err.ename, err.evalue, err.traceback);
        nl::json reply;
        reply["status"]    = "error";
        reply["ename"]     = err.ename;
        reply["evalue"]    = err.evalue;
        reply["traceback"] = err.traceback;
        cb(reply);
        return;
    }

    if (!vis_void(last_value)) {
        /* render_value's scm_write_shared can itself raise (e.g. on a
         * deeply nested but non-circular result -- check_c_stack_depth's
         * stack-overflow guard, src/runtime.c) -- and we're outside any
         * SCM_PROTECT at this point (the per-form one in the loop above
         * has already exited normally). Without its own protection
         * here, that would hit scm_raise_val's no-current_handler
         * fallback and abort() the whole kernel process while just
         * trying to report a successful cell's result. Same reasoning
         * as render_error's identical fix. */
        std::string rendered;
        ExnHandler h2;
        SCM_PROTECT(h2, {
            rendered = render_value(last_value);
        }, {
            rendered = "<error rendering result>";
            vm_reset();
        });
        nl::json data;
        data["text/plain"] = rendered;
        publish_execution_result(execution_counter, data, nl::json::object());
    }

    nl::json reply;
    reply["status"] = "ok";
    reply["payload"] = nl::json::array();
    reply["user_expressions"] = nl::json::object();
    cb(reply);
}

/* Completion/inspect (hover) are deferred -- the LSP module already
 * implements the same builtin-table + local-binding-collection logic
 * this would need, but it's compiled into a dlopen'd .so that expects
 * to run inside a live curry process, not linked into this standalone
 * kernel binary. Reusing it means lifting that logic out of
 * modules/lsp/lsp.c into something both can link; out of scope here. */
nl::json curry_interpreter::complete_request_impl(const std::string & /*code*/, int cursor_pos) {
    nl::json reply;
    reply["status"] = "ok";
    reply["matches"] = nl::json::array();
    reply["cursor_start"] = cursor_pos;
    reply["cursor_end"] = cursor_pos;
    reply["metadata"] = nl::json::object();
    return reply;
}

nl::json curry_interpreter::inspect_request_impl(const std::string & /*code*/, int /*cursor_pos*/, int /*detail_level*/) {
    nl::json reply;
    reply["status"] = "ok";
    reply["found"] = false;
    reply["data"] = nl::json::object();
    reply["metadata"] = nl::json::object();
    return reply;
}

nl::json curry_interpreter::is_complete_request_impl(const std::string &code) {
    int depth = 0;
    std::istringstream iss(code);
    std::string line;
    while (std::getline(iss, line)) depth += curry_line_depth(line.c_str());

    nl::json reply;
    if (depth > 0) {
        reply["status"] = "incomplete";
        reply["indent"] = "";
    } else {
        reply["status"] = "complete";
    }
    return reply;
}

nl::json curry_interpreter::kernel_info_request_impl() {
    nl::json reply;
    reply["implementation"] = "curry";
    reply["implementation_version"] = CURRY_VERSION;
    reply["banner"] = "Curry Scheme Jupyter kernel";
    nl::json lang_info;
    lang_info["name"] = "scheme";
    lang_info["mimetype"] = "text/x-scheme";
    lang_info["file_extension"] = ".scm";
    lang_info["version"] = CURRY_VERSION;
    reply["language_info"] = lang_info;
    reply["status"] = "ok";
    return reply;
}

nl::json curry_interpreter::shutdown_request_impl(bool /*restart*/) {
    nl::json reply;
    reply["status"] = "ok";
    return reply;
}

/* main.cpp builds this kernel with xeus::make_xserver_shell_main, the
 * split server: the control channel (which delivers this request) is
 * polled on its own thread, separate from the shell thread that's
 * blocked inside vm_run() running a cell -- see docs/reference/
 * jupyter-kernel.md's "Execution model". So this callback genuinely
 * runs concurrently with a busy cell. vm_interrupt_request() (src/
 * interrupt.c) sets a cross-thread atomic flag that vm_run()'s dispatch
 * loop checks at every instruction (the same per-instruction safepoint
 * the minor-GC poll and debugger hook already use); when set, it raises
 * an EC_INTERRUPTED condition, which unwinds through execute_request_
 * impl's ordinary SCM_PROTECT the same way any other raised condition
 * does -- no special-casing needed on the execute side. */
nl::json curry_interpreter::interrupt_request_impl() {
    vm_interrupt_request();
    nl::json reply;
    reply["status"] = "ok";
    return reply;
}

} // namespace curry_jupyter
