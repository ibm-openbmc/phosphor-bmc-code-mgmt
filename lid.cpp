#include "config.h"

#include "lid.hpp"

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
PHOSPHOR_LOG2_USING;

void Lid::validate(std::string filePath)
{
#ifdef WANT_ACCESS_KEY_VERIFY

    info("Update access key verification passed, {PATH}:", "PATH", filePath);

#endif
    return;
}

} // namespace manager
} // namespace software
} // namespace phosphor
