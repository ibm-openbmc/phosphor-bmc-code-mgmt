#pragma once
#include "xyz/openbmc_project/Collection/DeleteAll/server.hpp"

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Common/FactoryReset/server.hpp>
#include <xyz/openbmc_project/Control/FieldMode/server.hpp>
#include <xyz/openbmc_project/Software/LID/server.hpp>

#include <string>
#include <vector>

namespace phosphor
{
namespace software
{
namespace manager
{

using LidInherit = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Software::server::LID>;

class Lid : public LidInherit
{

  public:
    Lid(sdbusplus::bus_t& bus, const char* path) :
        LidInherit(bus, path)
    {}

    /**
     * @brief Validator for inband update
     */
     void validate(std::string filePath);

};

} // namespace manager
} // namespace software
} // namespace phosphor
