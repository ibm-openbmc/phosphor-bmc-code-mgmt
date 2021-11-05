#include "config.h"

#include "uak_verify.hpp"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

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
} // namespace image
} // namespace software
} // namespace phosphor
