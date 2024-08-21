#pragma once

#include <string>

namespace minimum_ship_level
{

constexpr auto resetFile = "/tmp/reset-msl";
constexpr auto mslFile = "msl-data";

/** @brief Version components */
struct Version
{
    uint8_t major;
    uint8_t minor;
    uint8_t rev;
};

/** @brief Verify if the current BMC version meets the min ship level
 *  @return true if the verification succeeded, false otherwise
 */
bool verify(const std::string& versionStr);

/** @brief Parse the version components into a struct
 *  @details User passes a version string in regex format (REGEX_BMC_MSL)
 *  at compilation time, this value is break down by parse function to allocate
 *  a struct so it can be compared position by position against the (BMC_MSL)
 *  also defined at compile time.
 * @param[in]  versionStr - The version string to be parsed
 * @param[out] version    - The version struct to be populated
 */
void parse(const std::string& versionStr, Version& version);

/** @brief Compare the versions provided
 *  @param[in] a - The first version to compare
 *  @param[in] b - The second version to compare
 *  @return 1 if a > b
 *          0 if a = b
 *         -1 if a < b
 */
int compare(const Version& a, const Version& b);

/** @brief Read the Min Ship Level from VPD */
std::string readSystemKeyword();

/** @brief Write the Min Ship Level to VPD */
void writeSystemKeyword(const std::string& value);

/** @brief Set the minimum ship level in VPD to the current version if it
 *         matches the format of the msl regex, or to a predefined value
 *         otherwise.
 */
void set();

/** @brief Create a file to indicate the subsequent code update process to set
 *         the minimum ship level to the image version used for the update.
 *         Default this value to the GA level in case the image version does not
 *         match the regex format.
 */
void reset();

/** @brief Check if the minimum ship level option is enabled
 *  @return true if enabled, false otherwise
 */
bool enabled();

/** @brief Get the minimum version
 *  @return[out] msl - Minimum version string
 */
std::string getMinimumVersion();

/** @brief Syncs the msl value in VPD and flash
 *  @details Sync the value in VPD to flash. If the VPD is blank, use the value
 *           in flash to write the VPD.
 */
void sync();

/** @brief Read the Min Ship Level from flash */
std::string readFlashValue();

/** @brief Write the Min Ship Level to flash */
void writeFlashValue(const std::string& value);

} // namespace minimum_ship_level
