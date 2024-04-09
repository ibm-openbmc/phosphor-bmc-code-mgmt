#pragma once

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Software/LID/server.hpp>

#include <chrono>
#include <random>
#include <string>

namespace phosphor
{
namespace software
{
namespace manager
{

using LidInherit = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::software::LID>;
namespace sdbusRule = sdbusplus::bus::match::rules;

class Lid : public LidInherit
{
  public:
    Lid(sdbusplus::bus_t& bus, const char* path) :
        LidInherit(bus, path), bus(bus),
        systemdSignals(
            bus,
            sdbusRule::type::signal() + sdbusRule::member("JobRemoved") +
                sdbusRule::path("/org/freedesktop/systemd1") +
                sdbusRule::interface("org.freedesktop.systemd1.Manager"),
            std::bind(std::mem_fn(&Lid::unitStateChange), this,
                      std::placeholders::_1))
    {}

    /**
     * @brief Validator for inband update
     *
     */
    void validate(std::string filePath);
    /**
     * @brief Create a image tar ball using lid files provided by host during
     * inband code update
     */
    void assembleCodeUpdateImage();

  private:
    sdbusplus::bus_t& bus;
    /** @brief Used to subscribe to dbus systemd signals **/
    sdbusplus::bus::match_t systemdSignals;

    /**
     * @brief Check if systemd state change is relevant to this object
     * Instance specific interface to handle the detected systemd state
     * change
     *
     * @param[in]  msg       - Data associated with subscribed signal
     *
     */
    void unitStateChange(sdbusplus::message_t& msg);

    /** @brief Generate a random number
     */
    std::mt19937 randomGen{static_cast<unsigned>(
        std::chrono::system_clock::now().time_since_epoch().count())};

    /** @brief Create a version interface under /xyz/openbmc_project/software
     *
     * Creation of an empty version interface triggers the item updater to
     * create an Invalid Activation object. Invalid activation is considered as
     * failure during assembling lids.
     */
    void createVersionInterface();
};

} // namespace manager
} // namespace software
} // namespace phosphor
