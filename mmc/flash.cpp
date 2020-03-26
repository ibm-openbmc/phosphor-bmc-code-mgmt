#include "config.h"

#include "flash.hpp"

#include "activation.hpp"

namespace phosphor
{
namespace software
{
namespace updater
{

namespace softwareServer = sdbusplus::xyz::openbmc_project::Software::server;

void Activation::flashWrite()
{
    if (!activationProgress)
    {
        activationProgress = std::make_unique<ActivationProgress>(bus, path);
    }

    // Enable systemd signals
    Activation::subscribeToSystemdSignals();

    // Set initial progress
    activationProgress->progress(20);

    auto serviceFile = "obmc-flash-bmc-mmc@" + versionId + ".service";
    auto method = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                      SYSTEMD_INTERFACE, "StartUnit");
    method.append(serviceFile, "replace");
    bus.call_noreply(method);

    return;
}

void Activation::onStateChanges(sdbusplus::message::message& msg)
{
    uint32_t newStateID{};
    sdbusplus::message::object_path newStateObjPath;
    std::string newStateUnit{};
    std::string newStateResult{};

    // Read the msg and populate each variable
    msg.read(newStateID, newStateObjPath, newStateUnit, newStateResult);

    auto serviceFile = "obmc-flash-bmc-mmc@" + versionId + ".service";

    if (newStateUnit == serviceFile)
    {
        // Unsubscribe from systemd signals
        unsubscribeFromSystemdSignals();

        // Remove version object from image manager
        deleteImageManagerObject();

        if (newStateResult == "done")
        {
            if (!redundancyPriority)
            {
                redundancyPriority =
                    std::make_unique<RedundancyPriority>(bus, path, *this, 0);
            }

            // Set activation progress to 100
            activationProgress->progress(100);

            // Set Activation value to active
            activation(softwareServer::Activation::Activations::Active);
        }
        else if (newStateResult == "failed")
        {
            // Set Activation value to Failed
            activation(softwareServer::Activation::Activations::Failed);
        }
    }

    return;
}

} // namespace updater
} // namespace software
} // namespace phosphor
