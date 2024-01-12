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

    /** @brief Method to verify if the UAK of the service pack is valid
     *
     *  @param[in] buildID - build ID of the service pack
     *
     *  @return true if the UAK is valid
     */
    bool checkIfUAKValid(const std::string& buildID);

    /** @brief Verify if the current image BUILD_ID meet the access key criteria
     *
     *  @param[in] gaDate - the GA date of the service pack
     *  @param[in] version - version/G level of the service pack
     *  @param[in] isHiper - flag to indicate if the SP is a HIPER service pack
     *
     *  @return true if the verification succeeded, false otherwise
     */
    bool verify(const std::string& gaDate, const std::string& version,
                bool isHiper);

    /** @brief Syncs the update access key found in VPD and flash memory */
    void sync();

  private:
    /** @brief Reads the D8 property from the inventory manager to retirieve the
     *         key information necessary for the update access key
     *  @param[in] objectPath - D-Bus object path
     *  @return [out] - value of the property
     */
    std::string getUpdateAccessExpirationDate(const std::string& objectPath);

    /** @brief Uses the VPD manager to write to the update access key property
     *  @param[in] key - uak date to write
     *  @param[in] objectPath - D-Bus object path
     */
    void writeUpdateAccessExpirationDate(const std::string& date,
                                         const std::string& objectPath);

    /** @brief Manifest file path */
    fs::path manifestPath;

    std::string buildIDTrunc{};
    std::string expirationDate{};
};

} // namespace image
} // namespace software
} // namespace phosphor
