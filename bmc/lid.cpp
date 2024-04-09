#include "config.h"

#include "lid.hpp"

#include "utils.hpp"
#include "version.hpp"

#include <arpa/inet.h>

#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Software/Image/error.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
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
namespace server = sdbusplus::server::xyz::openbmc_project::software;
using VersionClass = phosphor::software::manager::Version;
PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;
using namespace sdbusplus::error::xyz::openbmc_project::software::image;
using namespace phosphor::software::image;
using VersionClass = phosphor::software::manager::Version;

void Lid::validate(std::string filePath)
{
#ifdef WANT_ACCESS_KEY_VERIFY
    struct markerLid
    {
        uint32_t versionId;
        uint32_t offsetToMIKeywordSection;
        uint32_t offsetToIseriesMarker;
        uint32_t sizeOfMiKeyword;
        char miKeyword[40];
        char lastDisruptiveApplyFixLevel[3];
        char lastDisruptiveActivationFSP[3];
        char lastdisruptiveActivationPHYP[3];
        char lastDisruptiveActivationPFW[3];
        char lastChangedFixLevelForFSP[3];
        char lastChangedFixLevelForPHYP[3];
        char lastChangedFixLevelForPFW[3];
        char reserved[3];
        uint32_t offsetToAdditionalDataFields;
    };
    markerLid ml;

    typedef struct
    {
        uint32_t SPFlagsOffset;
        uint32_t SPFlagsSize;
        uint32_t SPDateOffset;
        uint32_t SPDateSize;
    } markerFwIPAdfHeader_t;

    typedef struct
    {
        uint32_t SPFlags;
        char SPDate[9];
    } fippAdfData_t;

    // Open the Marker LID file and copy contents to the markerLid structure
    std::ifstream efile(filePath, std::ios::in | std::ios::binary);
    efile.read(reinterpret_cast<char*>(&ml), sizeof(ml));

    using UpdateAccessKey = phosphor::software::image::UpdateAccessKey;
    UpdateAccessKey updateAccessKey("");
    uint32_t offBuff = htonl(ml.offsetToAdditionalDataFields);
    uint32_t adfCount;
    uint32_t adfSize;
    uint32_t adfSignature;
    markerFwIPAdfHeader_t markerFippAdfHeader;
    std::string gaDate{};
    std::string version{};

    // Read the total number of ADFs present in the Marker LID file
    efile.seekg(offBuff, efile.beg);
    efile.read(reinterpret_cast<char*>(&adfCount), sizeof(adfCount));

    // Loop through the ADFs to find respective ADF
    for (uint8_t i = 0; i < htonl(adfCount); i++)
    {
        // Read the Size of ADF
        efile.read(reinterpret_cast<char*>(&adfSize), sizeof(adfSize));

        // Read the ADF signature Eg: FIPP, DSPNM, I5FX, HMCV, SECV etc
        efile.read(reinterpret_cast<char*>(&adfSignature),
                   sizeof(adfSignature));

        // If the ADF is FIPP aka the firmware IP ADF
        if (htonl(adfSignature) == markerAdfFippSig)
        {
            fippAdfData_t fippData;

            // Read the ADF header into markerFippAdfHeader structure
            efile.read(reinterpret_cast<char*>(&markerFippAdfHeader),
                       sizeof(markerFippAdfHeader));

            // Reading SP flag which indicates weather the service pack is an
            // emergency service pack or one off service
            efile.read(reinterpret_cast<char*>(&fippData.SPFlags),
                       sizeof(fippData.SPFlags));

            // Reading the GA date or the date when the service pack was
            // released
            efile.read(fippData.SPDate, sizeof(fippData.SPDate));

            if (htonl(fippData.SPFlags) == hiperSPFlag)
            {
                isHiper = true;
            }

            gaDate = fippData.SPDate;

            version = std::string(&ml.miKeyword[2], &ml.miKeyword[6]);
            // Calling the UAK verify method
            updateAccessKey.verify(gaDate, version, isHiper);
            isHiper = false;
            break;
        }
        else
        {
            // seek to the next ADF in the Marker LID
            uint32_t nextADFOff = htonl(adfSize) - sizeof(adfSize) -
                                  sizeof(adfSignature);
            efile.seekg(nextADFOff, efile.cur);
        }
    }
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
