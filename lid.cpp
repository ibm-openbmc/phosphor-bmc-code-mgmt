#include "config.h"

#include "lid.hpp"

#include <elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Software/LID/error.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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
using ExpiredAccessKeyFail =
    sdbusplus::xyz::openbmc_project::Software::LID::Error::ExpiredAccessKey;

void Lid::validate(std::string filePath)
{
#ifdef WANT_ACCESS_KEY_VERIFY

    using UpdateAccessKey = phosphor::software::image::UpdateAccessKey;
    UpdateAccessKey updateAccessKey("");
    uint32_t offBuff = 0x8;
    uint32_t length = 0x4;
    std::string target = "FIPP";
    std::string signature{};
    std::string gaDate{};
    std::vector<uint8_t> resp;
    size_t fileSize = fs::file_size(filePath);
    std::cout << "fileSize=" << fileSize << std::endl;
    resp.resize(length);
    auto fileResp = reinterpret_cast<char*>(resp.data());

    std::cout << "fileResp =" << fileResp << std::endl;

    std::ifstream efile(filePath, std::ios::in | std::ios::binary);
    if (efile)
    {
        efile.seekg(offBuff);
        efile.read(fileResp, length);
        offBuff = resp.back() - 0x4; // offset to iSeries

        efile.seekg(offBuff, efile.beg);
        efile.read(fileResp, length);
        offBuff = resp.back(); // offset to ADF
        offBuff += 0x4;
        uint32_t adfBuff = offBuff - 1;
        efile.seekg(adfBuff, efile.beg);
        efile.read(fileResp, length);

        auto nadf = reinterpret_cast<char*>(resp.data());
        std::stringstream ss;
        const char* it = nadf;
        const char* end = nadf + std::strlen(nadf);
        for (; it != end; ++it)
            ss << std::hex << unsigned(*it);

        unsigned result;
        ss >> result;
        std::cout << "adfBuff=" << adfBuff << std::endl;
        std::cout << "nadf=" << std::hex << result << std::endl;
        std::cout << "nadf=" << std::setfill('0') << std::setw(2) << std::hex
                  << (uint16_t)result + 0 << std::endl;
        for (uint8_t i = 0; i <= result; i++)
        {
            std::cout << "i=" << (uint16_t)i << std::endl;
        }

        for (uint8_t i = 0; i <= result; i++)
        {
            // get size
            uint32_t tempBuff = offBuff;
            std::cout << "get size; tempBuff=" << tempBuff << std::endl;
            efile.seekg(tempBuff, efile.beg);
            efile.read(fileResp, length);
            uint32_t size = resp.back();

            if (!size)
            {
                break;
            }

            std::cout << "size=" << size << std::endl;
            ;
            // get signature
            tempBuff += 0x4;
            std::cout << "get signature; tempBuff=" << tempBuff << std::endl;
            efile.seekg(tempBuff, efile.beg);
            efile.read(fileResp, length);
            signature.assign(reinterpret_cast<const char*>(resp.data()),
                             resp.size());

            std::cout << "Signature=" << signature << std::endl;

            if (target == signature)
            {
                //  if ADF == FIPP call UAK verify by passing Marker lid date
                efile.seekg((offBuff + 0x8), efile.beg);
                efile.read(fileResp, length);
                uint32_t flagOff = offBuff + resp.back();
                efile.seekg(flagOff, efile.beg);
                efile.read(fileResp, length);
                efile.seekg((offBuff + 0x10), efile.beg);
                efile.read(fileResp, length);
                uint32_t gaOff = offBuff + resp.back();

                length = 0x8;
                resp.resize(length);
                auto fileRespGA = reinterpret_cast<char*>(resp.data());
                efile.seekg(gaOff, efile.beg);
                efile.read(fileRespGA, length);
                gaDate.assign(reinterpret_cast<const char*>(resp.data()),
                              resp.size());
                bool uak = updateAccessKey.verify(gaDate);
                std::cout << std::boolalpha << " uak verify=" << uak
                          << std::endl;
            }

            if (tempBuff >= fileSize)
            {
                return;
            }

            offBuff += size;
            std::cout << "offBuff = " << offBuff << std::endl;
        }
    }

    efile.close();

#endif
    return;
}

} // namespace manager
} // namespace software
} // namespace phosphor
