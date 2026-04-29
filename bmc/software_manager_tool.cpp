#include "code_updater_manager.hpp"

#include <CLI/CLI.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdeventplus/event.hpp>

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;

    CLI::App app{"OpenBMC software manager tool"};

    std::string imagePath{};

    app.add_option(
        "--codeupdate", imagePath,
        "Perform code update with new image on specified path");

    CLI11_PARSE(app, argc, argv);

    if (imagePath.empty())
    {
        lg2::error("The image path passed in is empty.");
        return -1;
    }

    fs::path sourceImagePath = fs::path{imagePath};

#ifdef START_UPDATE_DBUS_INTEFACE

    sdbusplus::async::context ctx;
    phosphor::software::manager::CodeUpdateManager manager(ctx, sourceImagePath);
    ctx.run();

#else

    // Dbus constructs
    auto bus = sdbusplus::bus::new_default();

    // Get a default event loop
    auto event = sdeventplus::Event::get_default();

    phosphor::software::manager::CodeUpdateManager manager(bus, event, sourceImagePath);

    // Attach the bus to sd_event to service user requests
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    event.loop();

#endif // START_UPDATE_DBUS_INTEFACE

    return 0;
}
