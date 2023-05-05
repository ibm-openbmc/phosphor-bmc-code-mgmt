#include "config.h"

#include "lid.hpp"

#include "utils.hpp"
#include "version.hpp"

#include <elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <string>

#ifdef WANT_ACCESS_KEY_VERIFY
#include "uak_verify.hpp"
#endif

namespace phosphor
{
namespace software
{
namespace manager
{
namespace server = sdbusplus::xyz::openbmc_project::Software::server;
using VersionClass = phosphor::software::manager::Version;
PHOSPHOR_LOG2_USING;

void Lid::validate(std::string filePath)
{
#ifdef WANT_ACCESS_KEY_VERIFY

    info("Update access key verification passed, {PATH}:", "PATH", filePath);

#endif
    return;
}

void Lid::assembleCodeUpdateImage()
{
    info("InbandCodeUpdate: assembleCodeUpdateImage\n");
    utils::subscribeToSystemdSignals(bus);
    auto method = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                      SYSTEMD_INTERFACE, "StartUnit");
    method.append("assemble-lids.service", "replace");
    bus.call_noreply(method);
}

void Lid::unitStateChange(sdbusplus::message_t& msg)
{
    uint32_t newStateID{};
    sdbusplus::message::object_path newStateObjPath;
    std::string newStateUnit{};
    std::string newStateResult{};

    // Read the msg and populate each variable
    msg.read(newStateID, newStateObjPath, newStateUnit, newStateResult);

    auto assembleLidServiceFile = "assemble-lids.service";

    if (newStateUnit == assembleLidServiceFile)
    {
        info(
            "InbandCodeUpdate: Service  - {SERVICEFILE} and Service Return value - {SERVICERESULT}",
            "SERVICEFILE", newStateUnit, "SERVICERESULT", newStateResult);
        // unsubscribe to systemd signals
        utils::unsubscribeFromSystemdSignals(bus);

        if (newStateResult == "done")
        {
            info(
                "InbandCodeUpdate: Assemble Code update image is completed successfully.");
        }
        else
        {
            // Create the empty version object path
            error("InbandCodeUpdate: Assemble Code update image Failed");
            createVersionInterface();
        }
    }
}

void Lid::createVersionInterface()
{
    // Creating a Version interface under Updater service to provide the failed
    // result of assemble code update image task. This result is read by PLDM
    // service
    auto versionId = VersionClass::getId(std::to_string(randomGen()));

    auto path = std::string{SOFTWARE_OBJPATH} + '/' + versionId;
    info("InbandCodeUpdate - Created Version interface {VERSION}", "VERSION",
         path);

    auto version = "null";

    auto dummyErase = [](std::string /*entryId*/) {
        // Do nothing;
    };

    // Staging directory
    auto imageDir = "/var/lib/phosphor-software-manager/hostfw/staging";

    auto assembleLidVersion = std::make_unique<VersionClass>(
        bus, path, version, server::Version::VersionPurpose::BMC, "", imageDir,
        std::vector<std::string>(),
        std::bind(dummyErase, std::placeholders::_1), "");

    assembleLidVersion->deleteObject =
        std::make_unique<phosphor::software::manager::Delete>(
            bus, path, *assembleLidVersion);
}

} // namespace manager
} // namespace software
} // namespace phosphor
