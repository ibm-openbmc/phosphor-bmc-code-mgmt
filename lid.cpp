#include "config.h"

#include "lid.hpp"

#include "version.hpp"

#include <arpa/inet.h>

#include <elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <string>
#include <system_error>

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
namespace control = sdbusplus::xyz::openbmc_project::Control::server;
PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Software::Image::Error;
using namespace phosphor::software::image;
namespace Software = phosphor::logging::xyz::openbmc_project::Software;
namespace fs = std::filesystem;

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
            uint32_t nextADFOff =
                htonl(adfSize) - sizeof(adfSize) - sizeof(adfSignature);
            efile.seekg(nextADFOff, efile.cur);
        }
    }

#endif
    return;
}

} // namespace manager
} // namespace software
} // namespace phosphor
