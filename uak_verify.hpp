#pragma once

#include <filesystem>
#include <string>

namespace phosphor
{
namespace software
{
namespace image
{

namespace fs = std::filesystem;

/** @class UpdateAccessKey
 *  @brief Contains access key verification and OpenBMC ipzvpd implementation.
 *  @details A concrete implementation for com.ibm.ipzvpd.UTIL D-Bus API.
 */
class UpdateAccessKey
{
  public:
    UpdateAccessKey() = delete;
    ~UpdateAccessKey() = default;
    /**
     * @brief Constructs UpdateAccessKey.
     * @param[in]  manifestPath - manifest path
     */
    UpdateAccessKey(const fs::path& manifestPath) : manifestPath(manifestPath)
    {}

    /** @brief Verify if the current image BUILD_ID meet the access key criteria
     *  @return true if the verification succeeded, false otherwise
     */
    bool verify();

  private:
    /** @brief Reads the D8 property from the inventory manager to retirieve the
     *         key information necessary for the update access key
     *  @return [out] - value of the property
     */
    std::string getUpdateAccessExpirationDate();

    /** @brief Get the BMC build_id string from the manifest file
     *  @return The build_id.
     */
    std::string getBuildID();

    /** @brief Manifest file path */
    fs::path manifestPath;
};

} // namespace image
} // namespace software
} // namespace phosphor
