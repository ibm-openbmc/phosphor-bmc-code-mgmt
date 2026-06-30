#pragma once

#include "utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>
#include <sdeventplus/event.hpp>

#include <filesystem>

namespace phosphor
{
namespace software
{
namespace manager
{

namespace fs = std::filesystem;
namespace MatchRules = sdbusplus::bus::match::rules;

class CodeUpdateManager
{
  public:
    ~CodeUpdateManager() = default;
    CodeUpdateManager() = delete;
    CodeUpdateManager(const CodeUpdateManager&) = delete;
    CodeUpdateManager(CodeUpdateManager&&) = default;
    CodeUpdateManager& operator=(const CodeUpdateManager&) = delete;
    CodeUpdateManager& operator=(CodeUpdateManager&&) = delete;

#ifdef START_UPDATE_DBUS_INTEFACE

    explicit CodeUpdateManager(sdbusplus::async::context& ctx,
                               const fs::path& sourceImagePath) :
        ctx(ctx), sourceImagePath(sourceImagePath)
    {
        ctx.spawn(run());
    }

    /** @brief Run the CodeUpdateManager */
    auto run() -> sdbusplus::async::task<void>;

  private:
    /** @brief D-Bus context. */
    sdbusplus::async::context& ctx;

    /** @brief Starts the firmware update.
     *  @param[in]  fd  - The file descriptor of the image to update.
     *  @return Success or Fail
     */
    auto startUpdate(int fd) -> sdbusplus::async::task<bool>;

#else
    explicit CodeUpdateManager(sdbusplus::bus_t& bus, sdeventplus::Event& event,
                               const fs::path& sourceImagePath) :
        bus(bus), event(event), isCLICodeUpdate(false),
        fwUpdateMatcher(
            bus,
            MatchRules::interfacesAdded() +
                MatchRules::path("/xyz/openbmc_project/software"),
            std::bind(std::mem_fn(&CodeUpdateManager::updateActivation), this,
                      std::placeholders::_1)),
        sourceImagePath(sourceImagePath)
    {
        if (!run())
        {
            lg2::error(
                "Failed to FW Update via CLI, sourceImagePath:{SRCIMGPATH}",
                "SRCIMGPATH", sourceImagePath);
            event.exit(0);
        }

        isCLICodeUpdate = true;
    }

    /** @brief Find the first file with a .tar extension according to the CLI
     *         file path.
     *
     *  @return Success or Fail
     */
    bool run();

  private:
    /** @brief Persistent sdbusplus DBus bus connection. */
    sdbusplus::bus_t& bus;

    /** sd event handler. */
    sdeventplus::Event& event;

    /** Indicates whether CLI codeupdate is going on. */
    bool isCLICodeUpdate;

    /** sdbusplus signal match for new image. */
    sdbusplus::bus::match_t fwUpdateMatcher;

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

#endif /* START_UPDATE_DBUS_INTEFACE */

    /** @brief Find the first file with a .tar extension according to the CLI
     *         file path and copy to IMG_UPLOAD_DIR
     *
     *  @return Success or Fail
     */
    bool copyImage();

    /** Source image path. */
    const fs::path& sourceImagePath;

    /** The destination path for copied over image file */
    fs::path imageDstPath;
};

} // namespace manager
} // namespace software
} // namespace phosphor
