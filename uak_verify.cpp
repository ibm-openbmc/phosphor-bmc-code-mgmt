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
using AccessKeyErr = sdbusplus::xyz::openbmc_project::Software::Version::Error::ExpiredAccessKey;
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

bool UpdateAccessKey::checkIfUAKValid(const std::string& buildID)
{
    
    constexpr auto motherboardObjectPath =
        "/xyz/openbmc_project/inventory/system/chassis/motherboard";
    expirationDate = getUpdateAccessExpirationDate(motherboardObjectPath);
    // Ensure that BUILD_ID date is in the YYYYMMDD format but truncating the
    // string to a length of 8 characters--excluding build's time information.
    
    buildIDTrunc = buildID.substr(0, 8);
    std::cout << "Inside verify, buildIDTrunc=" << buildIDTrunc << std::endl;
    std::cout << "Inside verify, expirationDate=" << expirationDate << std::endl;

    // If BuildID value is the designated value of 00000000, that means the
    // image being updated is an emergency service pack and should bypass the
    // UAK check.
    if (buildIDTrunc == "00000000")
    {
        debug("Access Key valid. BMC will begin activating.");
        return true;
    }
    try
    {
        boost::gregorian::date bd_date(
            boost::gregorian::from_undelimited_string(buildIDTrunc));
        boost::gregorian::date ed_date(
            boost::gregorian::from_undelimited_string(expirationDate));

        if (bd_date <= ed_date)
        {
            std::cout << "Inside verify, bd_date <= ed_date\n";
            debug("Access Key valid. BMC will begin activating.");
            return true;
        }
        std::cout << "Inside verify, bd_date <= ed_date, error\n";
        error(
            "Update Access Key validation failed. Expiration Date: {EXP_DATE}. "
            "Build date: {BUILD_ID}.",
            "EXP_DATE", expirationDate, "BUILD_ID", buildIDTrunc);
            elog<AccessKeyErr>(ExpiredAccessKey::EXP_DATE(expirationDate.c_str()),
                    ExpiredAccessKey::BUILD_ID(buildIDTrunc.c_str()));
        return false;
    }
    catch (...)
    {
        std::cout << "Inside verify, bd_date <= ed_date, exception\n";
        error(
            "Update Access Key validation failed. Expiration Date: {EXP_DATE}. "
            "Build date: {BUILD_ID}.",
            "EXP_DATE", expirationDate, "BUILD_ID", buildIDTrunc);
        elog<AccessKeyErr>(ExpiredAccessKey::EXP_DATE(expirationDate.c_str()),
                        ExpiredAccessKey::BUILD_ID(buildIDTrunc.c_str()));
        return false;
    }
}
bool UpdateAccessKey::verify(const std::string& gaDate, const std::string& version, bool isHiper)
{
    std::cout << "Inside verify, gaDate=" << gaDate << std::endl;
    std::cout << "Inside verify, version=" << version << std::endl;
    std::string expirationDate{};
    std::string buildID{};

    if (gaDate.empty())
    {
        buildID = getBuildID();
    }
    else
    {
        buildID = gaDate;
    }
    std::cout << "Inside verify, buildID=" << buildID << std::endl;

    if (!checkIfUAKValid(buildID))
    {
        std::string currExtendedVersion =
            VersionClass::getBMCExtendedVersion(OS_RELEASE_FILE);
        std::string currVersion = currExtendedVersion.substr(2, 4);
        if (isHiper && (version.compare(currVersion) == 0))
        {
            std::cout <<"Inside version.compare(currVersion) == 0\n";
            return true;
        }
        else
        {
            std::cout <<"Inside version.compare(currVersion) != 0\n";
            error("Update Access Key validation failed. Expiration Date: {EXP_DATE}. "
            "Build date: {BUILD_ID}.",
            "EXP_DATE", expirationDate, "BUILD_ID", buildIDTrunc);
            elog<AccessKeyErr>(ExpiredAccessKey::EXP_DATE(expirationDate.c_str()),
                               ExpiredAccessKey::BUILD_ID(buildIDTrunc.c_str()));
            return false;
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
