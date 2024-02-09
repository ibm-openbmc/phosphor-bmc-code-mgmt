#include "config.h"

#include "lid.hpp"

#include "msl_verify.hpp"
#include "utils.hpp"
#include "version.hpp"

#include <arpa/inet.h>

#include <elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
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
using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Software::Image::Error;
namespace Software = phosphor::logging::xyz::openbmc_project::Software;
namespace fs = std::filesystem;
using VersionClass = phosphor::software::manager::Version;

void Lid::validate(std::string filePath)
{
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
        uint32_t offset;
        uint32_t size;
    } markerSpnmAdfHeader_t;

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

    uint32_t offBuff = htonl(ml.offsetToAdditionalDataFields);
    uint32_t adfCount;
    uint32_t adfSize;
    uint32_t adfSignature;

    // Read the total number of ADFs present in the Marker LID file
    efile.seekg(offBuff, efile.beg);
    efile.read(reinterpret_cast<char*>(&adfCount), sizeof(adfCount));

    // Loop through the ADFs to find respective ADF
    for (uint8_t i = 0; i < htonl(adfCount); i++)
    {
        // Store the offset, this is the beginning of an ADF section
        auto adfStartOffset = efile.tellg();

        // Read the Size of ADF
        efile.read(reinterpret_cast<char*>(&adfSize), sizeof(adfSize));

        // Read the ADF signature Eg: FIPP, DSPNM, I5FX, HMCV, SECV etc
        efile.read(reinterpret_cast<char*>(&adfSignature),
                   sizeof(adfSignature));

        // If the ADF is SPNM aka the firmware version name
        if (htonl(adfSignature) == markerAdfSpnmSig)
        {
            markerSpnmAdfHeader_t markerSpnmAdfHeader;
            efile.read(reinterpret_cast<char*>(&markerSpnmAdfHeader),
                       sizeof(markerSpnmAdfHeader));

            // Seek to the beginning of the SPNM ADF
            efile.seekg(adfStartOffset, efile.beg);

            // Read the SPNM version name of format FWXXXX.YY
            efile.seekg(htonl(markerSpnmAdfHeader.offset), efile.cur);
            char spnmName[10];
            // Terminate with NULL to avoid garbage in the string
            spnmName[9] = '\0';
            efile.read(spnmName, sizeof(spnmName));
            fwVersion.append(spnmName);

            // The version name in the marker lid has a format FWXXXX.YY. The
            // minimum ship level check expects a version format fwXXXX.YY-ZZ,
            // where ZZ is the revision information. The missing revision has
            // the following implications:
            // 1. Need to build the version string to match the msl format by
            // replacing the initial 2 upper case characters with lower case,
            // and appending a revision number to the string. Choose 99 so that
            // it's higher than any revision set as the minimum ship level.
            // 2. The msl verification function sets the msl value to the
            // provided version string if the reset msl file exists. Therefore,
            // skip the msl verification if the reset msl file exists because
            // we don't want the new msl version to be set to the arbitrary
            // revision of -99.
            if (!std::filesystem::exists(minimum_ship_level::resetFile))
            {
                std::string version("fw");
                version.append(spnmName + 2);
                version.append("-99");

                // Only call the msl verification if the built string matches
                // the expected format.
                std::string mslRegex{REGEX_BMC_MSL};
                if (!mslRegex.empty())
                {
                    std::smatch match;
                    std::regex rx{mslRegex, std::regex::extended};
                    if (std::regex_search(version, match, rx))
                    {
                        minimum_ship_level::verify(version);
                    }
                }
            }
        }
        // If the ADF is FIPP aka the firmware IP ADF
        else if (htonl(adfSignature) == markerAdfFippSig)
        {
#ifdef WANT_ACCESS_KEY_VERIFY
            using namespace phosphor::software::image;
            using UpdateAccessKey = phosphor::software::image::UpdateAccessKey;
            UpdateAccessKey updateAccessKey("");
            markerFwIPAdfHeader_t markerFippAdfHeader;
            fippAdfData_t fippData;
            std::string gaDate{};
            std::string buildVersion{};

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

            if (htonl(fippData.SPFlags) == oneOffSPFlag)
            {
                isOneOff = true;
            }

            gaDate = fippData.SPDate;

            // Calling the UAK verify method
            std::regex pattern(R"(FW(\d+\.\d{2}))");
            std::smatch match;
            if (std::regex_search(fwVersion, match, pattern))
            {
                buildVersion = match[1];
            }

            updateAccessKey.verify(gaDate, buildVersion, isOneOff);
            isOneOff = false;
#endif
        }
        // seek to the next ADF in the Marker LID
        adfStartOffset += htonl(adfSize);
        efile.seekg(adfStartOffset, efile.beg);
    }
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
