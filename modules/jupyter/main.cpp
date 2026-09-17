#include "interpreter.hpp"

#include <xeus/xkernel.hpp>
#include <xeus/xkernel_configuration.hpp>
#include <xeus/xhelper.hpp>
#include <xeus-zmq/xserver_zmq.hpp>
#include <xeus-zmq/xzmq_context.hpp>

#include <memory>

int main(int argc, char *argv[]) {
    std::string connection_file = xeus::extract_filename(argc, argv);

    xeus::xconfiguration config = connection_file.empty()
        ? xeus::xconfiguration()
        : xeus::load_configuration(connection_file);

    auto context     = xeus::make_zmq_context();
    auto interpreter = std::make_unique<curry_jupyter::curry_interpreter>();

    xeus::xkernel kernel(config,
                          xeus::get_user_name(),
                          std::move(context),
                          std::move(interpreter),
                          xeus::make_xserver_default);
    kernel.start();
    return 0;
}
