#include "config.h"

#include "msl_verify.hpp"

#include "utils.hpp"
#include "version.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Software/Version/error.hpp>

#include <filesystem>
#include <regex>

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;
using InternalFailure =
    sdbusplus::error::xyz::openbmc_project::common::InternalFailure;

int minimum_ship_level::compare(const Version& versionToCompare,
                                const Version& mslVersion)
{
    if (versionToCompare.major > mslVersion.major)
    {
        return (1);
    }
    if (versionToCompare.major < mslVersion.major)
    {
        return (-1);
    }

    if (versionToCompare.minor > mslVersion.minor)
    {
        return (1);
    }
    if (versionToCompare.minor < mslVersion.minor)
    {
        return (-1);
    }

    if (versionToCompare.rev > mslVersion.rev)
    {
        return (1);
    }
    if (versionToCompare.rev < mslVersion.rev)
    {
        return (-1);
    }

    // Both string are equal and there is no need to make an upgrade return 0.
    return 0;
}

// parse Function copy  inpVersion onto outVersion in Version format
// {major,minor,rev}.
void minimum_ship_level::parse(const std::string& inpVersion,
                               Version& outVersion)
{
    std::smatch match;
    outVersion = {0, 0, 0};

    std::regex rx{REGEX_BMC_MSL, std::regex::extended};

    if (!std::regex_search(inpVersion, match, rx))
    {
        error("Unable to parse BMC version: {VERSION}", "VERSION", inpVersion);
        return;
    }

    outVersion.major = std::stoi(match[2]);
    outVersion.minor = std::stoi(match[3]);
    outVersion.rev = std::stoi(match[4]);
}

bool minimum_ship_level::verify(const std::string& versionManifest)
{
    auto isUninitialized = [](std::string mslStr) {
        return (mslStr.empty() || (mslStr.front() == '\0') ||
                isspace(mslStr.front()) || (mslStr.front() == '0'));
    };

    //  If there is no msl or mslRegex return upgrade is needed.
    std::string msl{BMC_MSL};
    std::string mslRegex{REGEX_BMC_MSL};

    // The MSL value was requested to be reset.
    if (std::filesystem::exists(resetFile))
    {
        // Predefined reset msl version string
        std::string resetStr{"fw1020.00-00"};
        if (!mslRegex.empty())
        {
            std::smatch match;
            std::regex rx{mslRegex, std::regex::extended};
            if (std::regex_search(versionManifest, match, rx))
            {
                resetStr = match.str(0);
            }
        }
        info("Resetting Minimum Ship Level to: {MSL}", "MSL", resetStr);
        writeSystemKeyword(resetStr);
    }

    if (msl.empty())
    {
        //  If the minimum level was not set as a compile-time option, check VPD
        msl = readSystemKeyword();
    }
    if ((isUninitialized(msl)) || mslRegex.empty())
    {
        return true;
    }

    // Define mslVersion variable and populate in Version format
    // {major,minor,rev} using parse function.

    std::string msl = getMinimumVersion();
    Version mslVersion = {0, 0, 0};
    parse(msl, mslVersion);

    // Define actualVersion variable and populate in Version format
    // {major,minor,rev} using parse function.
    std::string tmpStr{};

    tmpStr = versionManifest;
    Version actualVersion = {0, 0, 0};
    parse(versionManifest, actualVersion);

    // Compare actualVersion vs MSL.
    auto rc = compare(actualVersion, mslVersion);
    if (rc < 0)
    {
        using namespace phosphor::logging;
        using IncompatibleErr = sdbusplus::error::xyz::openbmc_project::
            software::version::Incompatible;
        using Incompatible =
            xyz::openbmc_project::software::version::Incompatible;
        std::string purpose =
            "xyz.openbmc_project.Software.Version.VersionPurpose.BMC";

        error(
            "BMC Minimum Ship Level ({MIN_VERSION}) NOT met by {ACTUAL_VERSION}",
            "MIN_VERSION", msl, "ACTUAL_VERSION", tmpStr, "VERSION_PURPOSE",
            purpose);
        elog<IncompatibleErr>(Incompatible::MIN_VERSION(msl.c_str()),
                              Incompatible::ACTUAL_VERSION(tmpStr.c_str()),
                              Incompatible::VERSION_PURPOSE(purpose.c_str()));
        return false;
    }

    return true;
}

std::string minimum_ship_level::readSystemKeyword()
{
    std::string minLevel{};
    auto bus = sdbusplus::bus::new_default();
    auto method = bus.new_method_call(
        "xyz.openbmc_project.Inventory.Manager",
        "/xyz/openbmc_project/inventory/system/chassis/motherboard",
        "org.freedesktop.DBus.Properties", "Get");
    method.append("com.ibm.ipzvpd.VSYS");
    method.append("FV");

    try
    {
        auto result = bus.call(method);
        if (!result.is_method_error())
        {
            std::variant<std::vector<uint8_t>> value;
            result.read(value);
            auto prop = std::get<std::vector<uint8_t>>(value);
            minLevel.assign(reinterpret_cast<const char*>(prop.data()),
                            prop.size());
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error reading VPD keyword: {ERROR}", "ERROR", e);
        report<InternalFailure>();
    }
    return minLevel;
}

void minimum_ship_level::writeSystemKeyword(const std::string& value)
{
    auto bus = sdbusplus::bus::new_default();
    constexpr auto vpdPath = "/com/ibm/VPD/Manager";
    constexpr auto vpdInterface = "com.ibm.VPD.Manager";
    constexpr auto objectPath =
        "/xyz/openbmc_project/inventory/system/chassis/motherboard";
    constexpr auto vpdRecord = "VSYS";
    constexpr auto vpdKeyword = "FV";

    // The VPD field is 32 bytes long and the default value is all 'spaces'. Pad
    // the string with spaces to avoid leftover characters from a previous value
    // that may had been longer.
    std::vector<uint8_t> vpdValue(value.begin(), value.end());
    vpdValue.insert(vpdValue.end(), 32 - vpdValue.size(), ' ');

    try
    {
        auto service = utils::getService(bus, vpdPath, vpdInterface);
        auto method = bus.new_method_call(service.c_str(), vpdPath,
                                          vpdInterface, "WriteKeyword");
        method.append(static_cast<sdbusplus::message::object_path>(objectPath),
                      vpdRecord, vpdKeyword, vpdValue);
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error writing VPD keyword to {MIN_LEVEL}: {ERROR}", "MIN_LEVEL",
              value, "ERROR", e);
        report<InternalFailure>();
    }
}

void minimum_ship_level::set()
{
    // Predefined msl version string
    std::string msl{"fw1030.00-00"};
    std::string mslRegex{REGEX_BMC_MSL};

    auto version =
        phosphor::software::manager::Version::getBMCVersion(OS_RELEASE_FILE);

    if (!mslRegex.empty())
    {
        std::smatch match;
        std::regex rx{mslRegex, std::regex::extended};
        if (std::regex_search(version, match, rx))
        {
            msl = match.str(0);
        }
    }

    info("Current version: {VERSION}. Setting Minimum Ship Level to: {MSL}",
         "VERSION", version, "MSL", msl);

    writeSystemKeyword(msl);
}

void minimum_ship_level::reset()
{
    std::ofstream outputFile(resetFile);
}
