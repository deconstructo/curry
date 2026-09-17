#ifndef CURRY_JUPYTER_INTERPRETER_HPP
#define CURRY_JUPYTER_INTERPRETER_HPP

#include <xeus/xinterpreter.hpp>
#include <nlohmann/json.hpp>

namespace nl = nlohmann;

namespace curry_jupyter {

/* Bridges Jupyter's xinterpreter protocol onto curry's own compiled-VM
 * execution path -- the exact same compiler_compile()+vm_run() sequence
 * the interactive REPL uses per top-level form (src/main.c), not the
 * tree-walker (eval()). One curry_interpreter instance lives for the
 * whole kernel process, so its GLOBAL_ENV/vm state persists across
 * cells the same way it persists across REPL inputs. */
class curry_interpreter : public xeus::xinterpreter {
public:
    curry_interpreter() = default;
    ~curry_interpreter() override = default;

private:
    void configure_impl() override;

    void execute_request_impl(xeus::xinterpreter::send_reply_callback cb,
                               int execution_counter,
                               const std::string &code,
                               xeus::execute_request_config config,
                               nl::json user_expressions) override;

    nl::json complete_request_impl(const std::string &code, int cursor_pos) override;
    nl::json inspect_request_impl(const std::string &code, int cursor_pos, int detail_level) override;
    nl::json is_complete_request_impl(const std::string &code) override;
    nl::json kernel_info_request_impl() override;
    nl::json shutdown_request_impl(bool restart) override;
    nl::json interrupt_request_impl() override;
};

} // namespace curry_jupyter

#endif
