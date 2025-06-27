#include "config.h"

#include "item_updater_helper.hpp"

#include "utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/exception.hpp>

#include <thread>

namespace phosphor
{
namespace software
{
namespace updater
{

PHOSPHOR_LOG2_USING;

void Helper::setEntry(const std::string& /* entryId */, uint8_t /* value */)
{
    // Empty
}

void Helper::clearEntry(const std::string& /* entryId */)
{
    // Empty
}

void Helper::cleanup()
{
    // Empty
}

void Helper::factoryReset()
{
    // Create informational log
    constexpr auto logObjPath = "/xyz/openbmc_project/logging";
    constexpr auto logInterface = "xyz.openbmc_project.Logging.Create";
    constexpr auto logSeverity =
        "xyz.openbmc_project.Logging.Entry.Level.Informational";
    constexpr auto logMessage =
        "xyz.openbmc_project.Software.Version.Info.FactoryResetRequested";
    try
    {
        auto service = utils::getService(bus, logObjPath, logInterface);
        auto method = bus.new_method_call(service.c_str(), logObjPath,
                                          logInterface, "Create");
        std::map<std::string, std::string> data{};
        method.append(logMessage, logSeverity, data);
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("Error creating log: {ERROR}", "ERROR", e);
    }

    // Mark the read-write partition for recreation upon reboot.
    utils::execute("/sbin/fw_setenv", "rwreset", "true");

    // Wait a few seconds for the log to be reported
    constexpr auto logWait = std::chrono::seconds(3);
    std::this_thread::sleep_for(logWait);
}

void Helper::removeVersion(const std::string& flashId)
{
    auto method = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                      SYSTEMD_INTERFACE, "StartUnit");
    auto serviceFile = "obmc-flash-mmc-remove@" + flashId + ".service";
    method.append(serviceFile, "replace");
    bus.call_noreply(method);

    // Wait a few seconds for the service file to finish, otherwise the BMC may
    // start the update while the image is still being deleted.
    constexpr auto removeWait = std::chrono::seconds(3);
    std::this_thread::sleep_for(removeWait);
}

void Helper::updateUbootVersionId(const std::string& flashId)
{
    auto method = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                      SYSTEMD_INTERFACE, "StartUnit");
    auto serviceFile = "obmc-flash-mmc-setprimary@" + flashId + ".service";
    method.append(serviceFile, "replace");
    bus.call_noreply(method);

    // Wait a few seconds for the service file to finish, otherwise the BMC may
    // be rebooted while pointing to a non-existent version.
    constexpr auto setPrimaryWait = std::chrono::seconds(3);
    std::this_thread::sleep_for(setPrimaryWait);
}

void Helper::mirrorAlt()
{
    auto method = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                      SYSTEMD_INTERFACE, "StartUnit");
    auto serviceFile = "obmc-flash-mmc-mirroruboot.service";
    method.append(serviceFile, "replace");
    bus.call_noreply(method);
}

} // namespace updater
} // namespace software
} // namespace phosphor
