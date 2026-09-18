#include "interpreter.hpp"

#include <xeus/xkernel.hpp>
#include <xeus/xkernel_configuration.hpp>
#include <xeus/xhelper.hpp>
#include <xeus-zmq/xserver_zmq_split.hpp>
#include <xeus-zmq/xzmq_context.hpp>

#include <memory>

int main(int argc, char *argv[]) {
    std::string connection_file = xeus::extract_filename(argc, argv);

    xeus::xconfiguration config = connection_file.empty()
        ? xeus::xconfiguration()
        : xeus::load_configuration(connection_file);

    auto context     = xeus::make_zmq_context();
    auto interpreter = std::make_unique<curry_jupyter::curry_interpreter>();

    /* make_xserver_shell_main (the split server) runs the control
     * channel on its own thread, separate from the shell thread that
     * blocks inside vm_run() for a busy cell -- required for
     * interrupt_request to be received (and processed) while a cell is
     * running at all. The non-split xeus::make_xserver_default polls
     * both channels from one thread/loop, so a control-channel message
     * queues up behind a blocked shell handler and is never even read
     * until the cell finishes on its own -- see docs/reference/
     * jupyter-kernel.md's "Execution model" and issue #232. */
    xeus::xkernel kernel(config,
                          xeus::get_user_name(),
                          std::move(context),
                          std::move(interpreter),
                          xeus::make_xserver_shell_main);
    kernel.start();
    return 0;
}
