#include "msl_verify.hpp"

#include <CLI/CLI.hpp>

int main(int argc, char** argv)
{
    CLI::App app{"OpenBMC software manager tool"};

    bool setMinLevel = false;
    bool resetMinLevel = false;

    app.add_flag(
        "--setminlevel", setMinLevel,
        "Set the minimum ship level to the running version of the system");
    app.add_flag("--resetminlevel", resetMinLevel,
                 "Reset the minimum ship level to allow a firmware dowgrade");

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

    return 0;
}
