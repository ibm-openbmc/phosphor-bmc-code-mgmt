#include "software.hpp"

#include "device.hpp"
#include "software_update.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async/context.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Software/Activation/aserver.hpp>
#include <xyz/openbmc_project/Software/Update/aserver.hpp>
#include <xyz/openbmc_project/State/Host/client.hpp>

PHOSPHOR_LOG2_USING;

using namespace phosphor::software;
using namespace phosphor::software::device;
using namespace phosphor::software::config;
using namespace phosphor::software::update;

const static std::string baseObjPathSoftware = "/xyz/openbmc_project/software/";

SoftwareActivationProgress::SoftwareActivationProgress(
    sdbusplus::async::context& ctx, const char* objPath) :
    ActivationProgress(ctx, objPath)
{
    // This prevents "Conditional jump or move depends on uninitialised
    // value(s)"
    // when properties are updated for the first time
    progress_ = 0;
}

void SoftwareActivationProgress::setProgress(int progressArg)
{
    progress(progressArg);
}

Software::Software(sdbusplus::async::context& ctx, Device& parent) :
    Software(ctx, parent, getRandomSoftwareId(parent))
{}

Software::Software(sdbusplus::async::context& ctx, Device& parent,
                   const std::string& swid) :
<<<<<<< HEAD
    SoftwareActivation(ctx, (baseObjPathSoftware + swid).c_str(),
                       Activation::properties_t{Activations::NotReady,
                                                RequestedActivations::None}),
<<<<<<< HEAD
    parentDevice(parent), swid(swid), objectPath(baseObjPathSoftware + swid),
=======
    SoftwareActivation(ctx, (baseObjPathSoftware + swid).c_str()),
    objectPath(baseObjPathSoftware + swid), parentDevice(parent), swid(swid),
>>>>>>> parent of df62819 (common: use aserver property constructor)
=======
    objectPath(baseObjPathSoftware + swid), parentDevice(parent), swid(swid),
>>>>>>> parent of dbb7015 (common: make Software::objectPath const)
    ctx(ctx)
{
    // initialize the members of our base class to prevent
    // "Conditional jump or move depends on uninitialised value(s)"
    activation_ = Activations::NotReady;
    requested_activation_ = RequestedActivations::None;

    std::string objPath = baseObjPathSoftware + swid;

    debug("{SWID}: created dbus interfaces on path {OBJPATH}", "SWID", swid,
          "OBJPATH", objPath);
};

long int Software::getRandomId()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned int seed = ts.tv_nsec ^ getpid();
    srandom(seed);
    return random() % 10000;
}

std::string Software::getRandomSoftwareId(Device& parent)
{
    return std::format("{}_{}", parent.config.configName, getRandomId());
}

sdbusplus::async::task<> Software::createInventoryAssociations(bool isRunning)
{
    debug("{SWID}: setting association definitions", "SWID", swid);

    std::string endpoint = "";

    try
    {
        endpoint = co_await parentDevice.config.getInventoryItemObjectPath(ctx);
    }
    catch (std::exception& e)
    {
<<<<<<< HEAD
        error("Failed to create association with {ERROR}", "ERROR", e.what());
        co_return;
    }

    if (endpoint.empty())
    {
        co_return;
=======
        error(e.what());
    }

    if (!associationDefinitions)
    {
        std::string path = objectPath;
        associationDefinitions =
            std::make_unique<SoftwareAssociationDefinitions>(ctx, path.c_str());
>>>>>>> parent of df62819 (common: use aserver property constructor)
    }

    createInventoryAssociation(isRunning, endpoint);
}

void Software::createInventoryAssociation(bool isRunning,
                                          std::string objectPath)
{
    std::vector<std::tuple<std::string, std::string, std::string>> assocs;

    if (endpoint.empty())
    {
        associationDefinitions->associations(assocs);
        co_return;
    }

    if (isRunning)
    {
        debug("{SWID}: creating 'running' association to {OBJPATH}", "SWID",
              swid, "OBJPATH", objectPath);
        std::tuple<std::string, std::string, std::string> assocRunning = {
            "running", "ran_on", objectPath};
        assocs.push_back(assocRunning);
    }
    else
    {
        debug("{SWID}: creating 'activating' association to {OBJPATH}", "SWID",
              swid, "OBJPATH", objectPath);
        std::tuple<std::string, std::string, std::string> assocActivating = {
            "activating", "activated_on", objectPath};
        assocs.push_back(assocActivating);
    }

    if (associationDefinitions)
    {
        associationDefinitions->associations(assocs);
    }
    else
    {
        associationDefinitions =
            std::make_unique<SoftwareAssociationDefinitions>(
                ctx, Software::objectPath.str.c_str(),
                SoftwareAssociationDefinitions::properties_t{assocs});
        associationDefinitions->emit_added();
    }
}

void Software::setVersion(const std::string& versionStr,
                          SoftwareVersion::VersionPurpose versionPurpose)
{
    debug("{SWID}: set version {VERSION}", "SWID", swid, "VERSION", versionStr);

    const bool emitSignal = !version;

    if (!version)
    {
        version =
            std::make_unique<SoftwareVersion>(ctx, objectPath.str.c_str());
    }

    version->version(versionStr);
    version->purpose(versionPurpose);

    if (emitSignal)
    {
        version->emit_added();
    }
}

std::optional<SoftwareVersion::VersionPurpose> Software::getPurpose()
{
    if (!version)
    {
        return std::nullopt;
    }
    return version->purpose();
}

void Software::setActivationBlocksTransition(bool enabled)
{
    if (!enabled)
    {
        activationBlocksTransition = nullptr;
        return;
    }

    std::string path = objectPath;
    activationBlocksTransition =
        std::make_unique<SoftwareActivationBlocksTransition>(ctx, path.c_str());
}

void Software::setActivation(SoftwareActivation::Activations act)
{
    activation(act);
}

void Software::enableUpdate(
    const std::set<RequestedApplyTimes>& allowedApplyTimes)
{
    if (updateIntf != nullptr)
    {
        error("[Software] update of {OBJPATH} has already been enabled",
              "OBJPATH", objectPath);
        return;
    }

    debug(
        "[Software] enabling update of {OBJPATH} (adding the update interface)",
        "OBJPATH", objectPath);

    updateIntf = std::make_unique<SoftwareUpdate>(ctx, objectPath.str.c_str(),
                                                  *this, allowedApplyTimes);
}
