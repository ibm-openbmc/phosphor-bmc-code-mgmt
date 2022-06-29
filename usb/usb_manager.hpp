#pragma once

#include "utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdeventplus/event.hpp>

#include <filesystem>

namespace phosphor
{
namespace usb
{
namespace fs = std::filesystem;
namespace MatchRules = sdbusplus::bus::match::rules;

class USBManager
{
  public:
    ~USBManager() = default;
    USBManager() = delete;
    USBManager(const USBManager&) = delete;
    USBManager(USBManager&&) = default;
    USBManager& operator=(const USBManager&) = delete;
    USBManager& operator=(USBManager&&) = default;

    explicit USBManager(sdbusplus::bus_t& bus, sdeventplus::Event& event,
                        const fs::path& devPath, const fs::path& usbPath) :
        bus(bus),
        event(event), devicePath(devPath), usbPath(usbPath),
        isUSBCodeUpdate(false),
        fwUpdateMatcher(bus,
                        MatchRules::interfacesAdded() +
                            MatchRules::path("/xyz/openbmc_project/software"),
                        std::bind(std::mem_fn(&USBManager::updateActivation),
                                  this, std::placeholders::_1)),
        bmcStateMatcher(
            bus,
            MatchRules::type::signal() +
                MatchRules::member("PropertiesChanged") +
                MatchRules::path("/xyz/openbmc_project/state/bmc0") +
                MatchRules::argN(0, "xyz.openbmc_project.State.BMC") +
                MatchRules::interface("org.freedesktop.DBus.Properties"),
            std::bind(std::mem_fn(&USBManager::bmcCheckState), this,
                      std::placeholders::_1))
    {
        try
        {
            auto bmcStateProp = utils::getProperty<std::string>(
                bus, "/xyz/openbmc_project/state/bmc0",
                "xyz.openbmc_project.State.BMC", "CurrentBMCState");
            if (bmcStateProp == "xyz.openbmc_project.State.BMC.BMCState.Ready")
            {
                if (!run())
                {
                    event.exit(0);
                }

                isUSBCodeUpdate = true;
            }
        }
        catch (const std::exception& e)
        {
            lg2::error("Failed in getting CurrentBMCState, ERROR:{ERROR}",
                       "ERROR", e.what());
        }
    }

    /** @brief Find the first file with a .tar extension according to the USB
     *         file path.
     *
     *  @return Success or Fail
     */
    bool run();

    /** @brief Creates an Activation D-Bus object.
     *
     * @param[in]  msg   - Data associated with subscribed signal
     */
    void updateActivation(sdbusplus::message_t& msg);

    /** @brief Set Apply Time to OnReset.
     *
     */
    void setApplyTime();

    /** @brief Method to set the RequestedActivation D-Bus property.
     *
     *  @param[in] path  - Update the object path of the firmware
     */
    void setRequestedActivation(const std::string& path);

    /**
     * @brief Set the USB progress to know if a code update on both sides have
     *        been performed.
     *
     * @return True if the USB code update can continue, False if both sides
     *         have been updated and the USB code update should not take place.
     */
    bool setUSBProgress();

    /** @brief Calls run() if bmc is in ready state.
     *
     * @param[in]  msg   - Data associated with subscribed signal
     */
    void bmcCheckState(sdbusplus::message::message& msg);

  private:
    /** @brief Persistent sdbusplus DBus bus connection. */
    sdbusplus::bus_t& bus;

    /** sd event handler. */
    sdeventplus::Event& event;

    /** The USB device path. */
    const fs::path& devicePath;

    /** The USB mount path. */
    const fs::path& usbPath;

    /** Indicates whether USB codeupdate is going on. */
    bool isUSBCodeUpdate;

    /** sdbusplus signal match for new image. */
    sdbusplus::bus::match_t fwUpdateMatcher;

    /** sdbusplus signal match for bmc ready state. */
    sdbusplus::bus::match_t bmcStateMatcher;
};

} // namespace usb
} // namespace phosphor
