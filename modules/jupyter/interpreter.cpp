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

#include <string>
#include <sstream>
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
    r.evalue = vis_string(message) ? curry_string_to_std(message) : render_value(exn);

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

} // namespace

void curry_interpreter::configure_impl() {
    curry_runtime_init();
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
        nl::json data;
        data["text/plain"] = render_value(last_value);
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
