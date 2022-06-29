#include "config.h"

#include "usb_manager.hpp"

#include <sys/mount.h>

namespace phosphor
{
namespace usb
{

bool USBManager::run()
{
    fs::path dir(usbPath);
    fs::create_directories(dir);

    auto rc = mount(devicePath.c_str(), usbPath.c_str(), "vfat", 0, NULL);
    if (rc)
    {
        lg2::error("Error ({ERRNO}) occurred during the mount call", "ERRNO",
                   errno);
        return false;
    }

    for (const auto& p : std::filesystem::directory_iterator(dir))
    {
        if (p.path().extension() == ".tar")
        {
            fs::path dstPath{IMG_UPLOAD_DIR / p.path().filename()};
            if (fs::exists(dstPath))
            {
                lg2::info(
                    "{DSTPATH} already exists in the /tmp/images directory, exit the upgrade",
                    "DSTPATH", p.path().filename());

                break;
            }

            if (!setUSBProgress())
            {
                utils::writeSuccess(bus);
                lg2::info("USB Code Update: Completed updating both sides");
                break;
            }
            try
            {
                return fs::copy_file(fs::absolute(p.path()), dstPath);
            }
            catch (const std::exception& e)
            {
                lg2::error("Error when copying {SRC} to /tmp/images: {ERROR}",
                           "SRC", p.path(), "ERROR", e.what());
            }

            break;
        }
    }

    return false;
}

bool USBManager::setUSBProgress()
{
    auto usbDir = fs::path(PERSIST_DIR) / "usb";
    fs::create_directories(usbDir);

    auto progress1 = usbDir / "usb1";
    if (!fs::exists(progress1))
    {
        std::ofstream os(progress1.c_str());
        utils::writeSideOneProgress(bus);
        return true;
    }

    auto progress2 = usbDir / "usb2";
    if (!fs::exists(progress2))
    {
        std::ofstream os(progress2.c_str());
        utils::writeSideTwoProgress(bus);
        return true;
    }

    fs::remove_all(usbDir);
    return false;
}

void USBManager::setApplyTime()
{
    utils::PropertyValue value =
        "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate";
    try
    {
        constexpr auto objectPath = "/xyz/openbmc_project/software/apply_time";
        constexpr auto interface = "xyz.openbmc_project.Software.ApplyTime";
        constexpr auto propertyName = "RequestedApplyTime";
        utils::setProperty(bus, objectPath, interface, propertyName, value);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to set RequestedApplyTime property, ERROR:{ERROR}",
                   "ERROR", e.what());
    }
}

void USBManager::setRequestedActivation(const std::string& path)
{
    utils::PropertyValue value =
        "xyz.openbmc_project.Software.Activation.RequestedActivations.Active";
    try
    {
        constexpr auto interface = "xyz.openbmc_project.Software.Activation";
        constexpr auto propertyName = "RequestedActivation";
        utils::setProperty(bus, path, interface, propertyName, value);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to set RequestedActivation property, ERROR:{ERROR}",
                   "ERROR", e.what());
    }

    return;
}

void USBManager::updateActivation(sdbusplus::message_t& msg)
{
    std::map<std::string, std::map<std::string, std::variant<std::string>>>
        interfaces;
    sdbusplus::message::object_path path;
    msg.read(path, interfaces);

    constexpr auto imageInterface = "xyz.openbmc_project.Software.Activation";
    constexpr auto readyPro =
        "xyz.openbmc_project.Software.Activation.Activations.Ready";
    for (auto& interface : interfaces)
    {
        if (interface.first != imageInterface)
        {
            continue;
        }

        try
        {
            auto imageProp = utils::getProperty<std::string>(
                bus, path.str, imageInterface, "Activation");

            if (imageProp == readyPro && isUSBCodeUpdate)
            {
                setApplyTime();
                setRequestedActivation(path.str);
                event.exit(0);
            }
        }
        catch (const std::exception& e)
        {
            lg2::error("Failed in getting Activation status, ERROR:{ERROR}",
                       "ERROR", e.what());
        }
    }
}

void USBManager::bmcCheckState(sdbusplus::message::message& msg)
{
    std::string interface, bmcState;
    std::map<std::string, std::variant<std::string>> properties;

    try
    {
        msg.read(interface, properties);

        for (const auto& p : properties)
        {
            if (p.first == "CurrentBMCState")
            {
                bmcState = std::get<std::string>(p.second);
                break;
            }
        }

        if (bmcState.empty())
        {
            return;
        }
        else if (bmcState == "xyz.openbmc_project.State.BMC.BMCState.Ready")
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
        lg2::error("Failed in getting CurrentBMCState, ERROR:{ERROR}", "ERROR",
                   e.what());
    }
}

} // namespace usb
} // namespace phosphor
