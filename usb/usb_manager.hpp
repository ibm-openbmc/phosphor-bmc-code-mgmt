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
    USBManager& operator=(USBManager&&) = delete;

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

    /** @brief Api to register activation state callback. This api will register
     *         callback to listen for activation state property change.
     *  @param[in] path - activation path
     */
    void listenActivationState(const std::string& path);

    /** @brief Displays failed progress code upon update failure.
     *
     * @param[in]  msg  - Data associated with subscribed signal
     */
    void activationChangeEvent(sdbusplus::message::message& msg);

    /**
     * @brief method to set the dbus Raw value Property with the obtained
     *        progress code from code update.
     *
     * @param[in] prgCode - the tuple containing primary and secondary progress
     *            code
     **/
    void setProgressSRC(
        const std::tuple<uint64_t, std::vector<uint8_t>>& prgCode,
        sdbusplus::bus::bus& bus);

    /**
     * @brief method to create tuple for dbus method that displays information
     *        to the op panel and adds an entry to the progress log
     *
     * @param[in] primArray - the progress Code SRC
     **/
    void writeProgressSRC(const std::vector<uint8_t>& primArray,
                          sdbusplus::bus::bus& bus);

    /**
     * @brief method to display success message on op panel during usb code
     *        update C100B103: The firmware installation has been completed
     *        successfully.
     *
     * @param[in] bus
     **/
    void writeSuccess(sdbusplus::bus::bus& bus);

    /**
     * @brief method to display same side installation message on op panel
     *        during usb code update C100B101: The firmware image is being
     *        installed on a side of the flash.
     *
     * @param[in] bus
     **/
    void writeSideOneProgress(sdbusplus::bus::bus& bus);

    /**
     * @brief method to display opposite side installation message on op panel
     *        during usb code update C100B102: The firmware image is being
     *        installed on the other side of the flash.
     *
     * @param[in] bus
     **/
    void writeSideTwoProgress(sdbusplus::bus::bus& bus);

    /**
     * @brief method to display fail message on op panel during usb code update
     *        C100B104 code is displayed, the firmware installation has failed.
     *
     * @param[in] bus
     **/
    void writeFail(sdbusplus::bus::bus& bus);

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
