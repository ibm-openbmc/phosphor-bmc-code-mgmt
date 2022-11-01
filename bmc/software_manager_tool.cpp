#include "code_updater_manager.hpp"
#include "msl_verify.hpp"

#include <CLI/CLI.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdeventplus/event.hpp>

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;

    bool setMinLevel = false;
    bool resetMinLevel = false;

    std::string imagePath{};

    CLI::App app{"OpenBMC software manager tool"};

    app.add_flag(
        "--setminlevel", setMinLevel,
        "Set the minimum ship level to the running version of the system");
    app.add_flag("--resetminlevel", resetMinLevel,
                 "Reset the minimum ship level to allow a firmware dowgrade");
    app.add_option(
        "--codeupdate", imagePath,
        "Perform code update with new image on specified path");

    CLI11_PARSE(app, argc, argv);

    if (setMinLevel)
    {
        minimum_ship_level::set();
    }
    else if (resetMinLevel)
    {
        minimum_ship_level::reset();
    }
    else
    {
        std::cout << app.help("", CLI::AppFormatMode::All) << std::endl;
    }

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
