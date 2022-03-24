#include "config.h"

#include "uak_verify.hpp"

#include "utils.hpp"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>

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

std::string UpdateAccessKey::getUpdateAccessExpirationDate()
{
    std::string date{};
    auto bus = sdbusplus::bus::new_default();
    auto method = bus.new_method_call("xyz.openbmc_project.Inventory.Manager",
                                      "/xyz/openbmc_project/inventory/system/"
                                      "chassis/motherboard",
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

void UpdateAccessKey::writeUpdateAccessExpirationDate(const std::string& date)
{
    auto bus = sdbusplus::bus::new_default();
    constexpr auto uakObjPath = "/com/ibm/VPD/Manager";
    constexpr auto uakInterface = "com.ibm.VPD.Manager";
    constexpr auto fruPath =
        "/xyz/openbmc_project/inventory/system/chassis/motherboard";
    constexpr auto fruRecord = "UTIL";
    constexpr auto fruKeyword = "D8";
    std::vector<uint8_t> uakData(date.begin(), date.end());

    try
    {
        auto service = utils::getService(bus, uakObjPath, uakInterface);
        auto method = bus.new_method_call(service.c_str(), uakObjPath,
                                          uakInterface, "WriteKeyword");
        method.append(static_cast<sdbusplus::message::object_path>(fruPath),
                      fruRecord, fruKeyword, uakData);
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error setting VPD keyword to {EXP_DATE}: {ERROR}", "EXP_DATE",
              date, "ERROR", e);
    }
}

std::string UpdateAccessKey::getBuildID()
{
    std::string buildIDKey = "BuildId";
    std::string buildIDValue{};
    std::string buildID{};

    std::ifstream efile(manifestPath);
    std::string line;

    while (getline(efile, line))
    {
        if (line.substr(0, buildIDKey.size()).find(buildIDKey) !=
            std::string::npos)
        {
            buildIDValue = line.substr(buildIDKey.size());
            std::size_t pos = buildIDValue.find_first_of('=') + 1;
            buildID = buildIDValue.substr(pos);
            break;
        }
    }
    efile.close();

    if (buildID.empty())
    {
        error("BMC build id is empty");
    }

    return buildID;
}

bool UpdateAccessKey::verify()
{
    std::string expirationDate{};
    std::string buildID{};
    buildID = getBuildID();
    expirationDate = getUpdateAccessExpirationDate();

    // Ensure that BUILD_ID date is in the YYYYMMDD format but truncating the
    // string to a length of 8 characters--excluding build's time information.
    std::string buildIDTrunc = buildID.substr(0, 8);

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
    }
    return false;
}

void UpdateAccessKey::sync()
{
    constexpr auto uakDateFile = "uak-data";
    auto flashDateFilePath = fs::path(PERSIST_DIR) / uakDateFile;
    auto backplaneDate = getUpdateAccessExpirationDate();

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
            // Write backplane date to flash memory
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
    }
    else if (!isUninitialized(flashDate))
    {
        // Write flash date to backplane date
        writeUpdateAccessExpirationDate(flashDate);
    }
}

} // namespace image
} // namespace software
} // namespace phosphor
