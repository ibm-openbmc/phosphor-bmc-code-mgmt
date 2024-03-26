#include "config.h"

#include "uak_verify.hpp"

#include "utils.hpp"
#include "version.hpp"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Software/Version/error.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace phosphor
{
namespace software
{
namespace image
{
PHOSPHOR_LOG2_USING;

using namespace phosphor::logging;
using VersionClass = phosphor::software::manager::Version;
using AccessKeyErr =
    sdbusplus::xyz::openbmc_project::Software::Version::Error::ExpiredAccessKey;
using ExpiredAccessKey =
    xyz::openbmc_project::Software::Version::ExpiredAccessKey;
std::string UpdateAccessKey::getUpdateAccessExpirationDate(
    const std::string& objectPath)
{
    std::string date{};
    auto bus = sdbusplus::bus::new_default();
    auto method = bus.new_method_call("xyz.openbmc_project.Inventory.Manager",
                                      objectPath.c_str(),
                                      "org.freedesktop.DBus.Properties", "Get");
    method.append("com.ibm.ipzvpd.UTIL");
    method.append("D8");

    try
    {
        auto result = bus.call(method);
        if (!result.is_method_error())
        {
            std::variant<std::vector<uint8_t>> value;
            result.read(value);
            auto prop = std::get<std::vector<uint8_t>>(value);
            date.assign(reinterpret_cast<const char*>(prop.data()),
                        prop.size());
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        debug("Error in getting Update Access Key expiration date: {ERROR}",
              "ERROR", e);
    }
    return date;
}

void UpdateAccessKey::writeUpdateAccessExpirationDate(
    const std::string& date, const std::string& objectPath)
{
    auto bus = sdbusplus::bus::new_default();
    constexpr auto uakObjPath = "/com/ibm/VPD/Manager";
    constexpr auto uakInterface = "com.ibm.VPD.Manager";
    constexpr auto fruRecord = "UTIL";
    constexpr auto fruKeyword = "D8";
    std::vector<uint8_t> uakData(date.begin(), date.end());

    try
    {
        auto service = utils::getService(bus, uakObjPath, uakInterface);
        auto method = bus.new_method_call(service.c_str(), uakObjPath,
                                          uakInterface, "WriteKeyword");
        method.append(static_cast<sdbusplus::message::object_path>(objectPath),
                      fruRecord, fruKeyword, uakData);
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error setting {PATH} VPD keyword to {EXP_DATE}: {ERROR}", "PATH",
              objectPath, "EXP_DATE", date, "ERROR", e);
    }
}

bool UpdateAccessKey::checkIfUAKValid(const std::string& buildID)
{
    constexpr auto motherboardObjectPath =
        "/xyz/openbmc_project/inventory/system/chassis/motherboard";
    expirationDate = getUpdateAccessExpirationDate(motherboardObjectPath);
    // Ensure that BUILD_ID date is in the YYYYMMDD format but truncating the
    // string to a length of 8 characters--excluding build's time information.
    buildIDTrunc = buildID.substr(0, 8);

    try
    {
        boost::gregorian::date bd_date(
            boost::gregorian::from_undelimited_string(buildIDTrunc));
        boost::gregorian::date ed_date(
            boost::gregorian::from_undelimited_string(expirationDate));

        if (bd_date <= ed_date)
        {
            debug("Access Key valid. BMC will begin activating.");
            return true;
        }
        error(
            "Update Access Key validation failed. Expiration Date: {EXP_DATE}. "
            "Build date: {BUILD_ID}.",
            "EXP_DATE", expirationDate, "BUILD_ID", buildIDTrunc);
        return false;
    }
    catch (...)
    {
        error(
            "Update Access Key validation failed. Expiration Date: {EXP_DATE}. "
            "Build date: {BUILD_ID}.",
            "EXP_DATE", expirationDate, "BUILD_ID", buildIDTrunc);
        return false;
    }
}
bool UpdateAccessKey::verify(const std::string& gaDate,
                             const std::string& version, bool isOneOff)
{
    std::string expirationDate{};
    std::string buildID{};
    buildID = gaDate;

    if (!checkIfUAKValid(buildID))
    {
        try
        {
            if (version.empty())
            {
                return false;
            }
            std::string versionID =
                VersionClass::getBMCVersion(OS_RELEASE_FILE);
            // skip the first two characters to get the GA level
            std::string currVersion = versionID.substr(2, 7);
            size_t dotPosition = currVersion.find('.');

            std::string currMajorVersion = currVersion.substr(0, dotPosition);
            int currentMajorVersion = stoi(currMajorVersion);

            std::string currMinorVersion = currVersion.substr(dotPosition + 1);
            int currentMinorVersion = stoi(currMinorVersion);
            int currentMinorVersionX = currentMinorVersion / 10;

            dotPosition = version.find('.');
            std::string tarMajorVersion = version.substr(0, dotPosition);
            int targetMajorVersion = stoi(tarMajorVersion);

            std::string tarMinorVersion = version.substr(dotPosition + 1);
            int targetMinorVersion = stoi(tarMinorVersion);
            int targetMinorVersionX = targetMinorVersion / 10;

            if (((targetMajorVersion == currentMajorVersion) &&
                 (targetMinorVersionX <= currentMinorVersionX)) ||
                isOneOff || (targetMajorVersion < currentMajorVersion))
            {
                return true;
            }
            else
            {
                error(
                    "Update Access Key validation failed. Expiration Date: {EXP_DATE}. "
                    "Build date: {BUILD_ID}.",
                    "EXP_DATE", expirationDate, "BUILD_ID", buildIDTrunc);
                elog<AccessKeyErr>(
                    ExpiredAccessKey::EXP_DATE(expirationDate.c_str()),
                    ExpiredAccessKey::BUILD_ID(buildIDTrunc.c_str()));
                return false;
            }
        }
        catch (const std::invalid_argument& e)
        {
            std::cerr << "Invalid argument: " << e.what() << std::endl;
        }
    }
    return true;
}

void UpdateAccessKey::sync()
{
    constexpr auto vpdInterface = "com.ibm.ipzvpd.UTIL";
    std::string backplaneDate{};
    std::string panelDate{};
    std::string motherboardObjectPath{};
    std::string panelObjectPath{};

    auto bus = sdbusplus::bus::new_default();
    try
    {
        auto subTreeResponse = utils::getSubTree(bus, vpdInterface);
        for (const auto& [path, object] : subTreeResponse)
        {
            for (const auto& [service, interfaces] : object)
            {
                for (const auto& interface : interfaces)
                {
                    if (interface ==
                        "xyz.openbmc_project.Inventory.Item.Board.Motherboard")
                    {
                        motherboardObjectPath = path;
                        backplaneDate = getUpdateAccessExpirationDate(
                            motherboardObjectPath);
                    }
                    else if (interface ==
                             "xyz.openbmc_project.Inventory.Item.Panel")
                    {
                        panelObjectPath = path;
                        panelDate =
                            getUpdateAccessExpirationDate(panelObjectPath);
                    }
                }
            }
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error in GetSubTree for interface {INTERFACE}: {ERROR}",
              "INTERFACE", vpdInterface, "ERROR", e);
        return;
    }

    constexpr auto uakDateFile = "uak-data";
    auto flashDateFilePath = fs::path(PERSIST_DIR) / uakDateFile;
    std::string flashDate{};
    if (fs::exists(flashDateFilePath))
    {
        std::ifstream inputFile(flashDateFilePath.string(), std::ios::in);
        if (inputFile)
        {
            inputFile >> flashDate;
            inputFile.close();
        }
    }

    auto isUninitialized = [](std::string uakStr) {
        return (uakStr.empty() || (uakStr.front() == '\0') ||
                isspace(uakStr.front()) || (uakStr.front() == '0'));
    };

    if (!isUninitialized(backplaneDate))
    {
        if (backplaneDate.compare(flashDate) != 0)
        {
            // Write backplane date to flash memory and panel date
            if (!fs::is_directory(flashDateFilePath.parent_path()))
            {
                fs::create_directories(flashDateFilePath.parent_path());
            }
            std::ofstream outputFile(flashDateFilePath.string(),
                                     std::ios::out | std::ios::trunc);
            if (outputFile)
            {
                outputFile << backplaneDate;
                outputFile.close();
            }
        }
        if ((backplaneDate.compare(panelDate) != 0) && !panelObjectPath.empty())
        {
            writeUpdateAccessExpirationDate(backplaneDate, panelObjectPath);
        }
    }
    else if (!isUninitialized(flashDate))
    {
        // Write flash date to backplane date and panel date
        if (!motherboardObjectPath.empty())
        {
            writeUpdateAccessExpirationDate(flashDate, motherboardObjectPath);
        }
        if ((flashDate.compare(panelDate) != 0) && !panelObjectPath.empty())
        {
            writeUpdateAccessExpirationDate(flashDate, panelObjectPath);
        }
    }
    else if (!isUninitialized(panelDate))
    {
        // Write panel date to backplane date and flash memory
        if (!motherboardObjectPath.empty())
        {
            writeUpdateAccessExpirationDate(panelDate, motherboardObjectPath);
        }
        if (!fs::is_directory(flashDateFilePath.parent_path()))
        {
            fs::create_directories(flashDateFilePath.parent_path());
        }
        std::ofstream outputFile(flashDateFilePath.string(),
                                 std::ios::out | std::ios::trunc);
        if (outputFile)
        {
            outputFile << panelDate;
            outputFile.close();
        }
    }
}

} // namespace image
} // namespace software
} // namespace phosphor
