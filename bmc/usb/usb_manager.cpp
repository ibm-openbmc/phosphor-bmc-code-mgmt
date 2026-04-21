#include "config.h"

#include "usb_manager.hpp"

#include <sys/mount.h>

#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/Software/ApplyTime/common.hpp>
#include <xyz/openbmc_project/Software/Update/client.hpp>

#include <system_error>

namespace phosphor
{
namespace usb
{

using Association = std::tuple<std::string, std::string, std::string>;
using Paths = std::vector<std::string>;

bool USBManager::copyImage()
{
    std::error_code ec;
    fs::path dir(usbPath);
    fs::create_directories(dir, ec);

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
            if (fs::exists(dstPath, ec))
            {
                lg2::info(
                    "{DSTPATH} already exists in the /tmp/images directory, exit the upgrade",
                    "DSTPATH", p.path().filename());

                break;
            }

            if (!setUSBProgress())
            {
                writeSuccess(bus);
                lg2::info("USB Code Update: Completed updating both sides");
                break;
            }
            try
            {
                imageDstPath = dstPath;
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

#ifdef START_UPDATE_DBUS_INTEFACE

auto findAssociatedUpdatablePath(sdbusplus::async::context& ctx)
    -> sdbusplus::async::task<Paths>
{
    constexpr auto associatedPath =
        "/xyz/openbmc_project/software/bmc/updateable";
    constexpr auto interface = "xyz.openbmc_project.Association";
    constexpr auto propertyName = "endpoints";

    co_return utils::getProperty<Paths>(ctx.get_bus(), associatedPath,
                                        interface, propertyName);
}

auto USBManager::startUpdate(int fd) -> sdbusplus::async::task<bool>
{
    using Updater = sdbusplus::client::xyz::openbmc_project::software::Update<>;
    using ApplyTimeIntf =
        sdbusplus::common::xyz::openbmc_project::software::ApplyTime;

    constexpr auto serviceName = "xyz.openbmc_project.Software.Manager";

    auto paths = co_await findAssociatedUpdatablePath(ctx);
    if (paths.size() != 1)
    {
        lg2::error("Failed to find associated updatable path");
        co_return false;
    }

    auto updater = Updater(ctx).service(serviceName).path(paths[0]);
    sdbusplus::message::object_path objectPath = co_await updater.start_update(
        fd, ApplyTimeIntf::RequestedApplyTimes::OnReset);
    if (objectPath.str.empty())
    {
        lg2::error("StartUpdate failed");
        co_return false;
    }
    lg2::info("StartUpdate succeeded, objectPath: {PATH}", "PATH", objectPath);

    co_return true;
}

auto USBManager::run() -> sdbusplus::async::task<void>
{
    auto res = copyImage();
    if (!res)
    {
        lg2::error("Failed to copy image from USB");
        co_return;
    }

    int fd = open(imageDstPath.c_str(), O_RDONLY);
    if (fd < 0)
    {
        lg2::error("Failed to open {PATH}", "PATH", imageDstPath);
        co_return;
    }

    co_await startUpdate(fd);

    ctx.request_stop();

    co_return;
}

#else

bool USBManager::run()
{
    return copyImage();
}

bool USBManager::setUSBProgress()
{
    auto usbDir = fs::path(PERSIST_DIR) / "usb";
    fs::create_directories(usbDir);

    auto progress1 = usbDir / "usb1";
    if (!fs::exists(progress1))
    {
        std::ofstream os(progress1.c_str());
        writeSideOneProgress(bus);
        return true;
    }

    auto progress2 = usbDir / "usb2";
    if (!fs::exists(progress2))
    {
        std::ofstream os(progress2.c_str());
        writeSideTwoProgress(bus);
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
                listenActivationState(path.str);
                setRequestedActivation(path.str);
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

void USBManager::listenActivationState(const std::string& path)
{
    static std::unique_ptr<sdbusplus::bus::match::match> activationState =
        std::make_unique<sdbusplus::bus::match::match>(
            bus,
            MatchRules::propertiesChanged(
                path, "xyz.openbmc_project.Software.Activation"),
            [this](sdbusplus::message::message& msg) {
        activationChangeEvent(msg);
    });
}

void USBManager::activationChangeEvent(sdbusplus::message::message& msg)
{
    std::string interface, activationStatus;
    std::map<std::string, std::variant<std::string>> properties;

    constexpr auto failedStatus =
        "xyz.openbmc_project.Software.Activation.Activations.Failed";
    constexpr auto activeStatus =
        "xyz.openbmc_project.Software.Activation.Activations.Active";
    auto usbDir = fs::path(PERSIST_DIR) / "usb";

    try
    {
        msg.read(interface, properties);

        for (const auto& p : properties)
        {
            if (p.first == "Activation")
            {
                activationStatus = std::get<std::string>(p.second);
                break;
            }
        }

        if (activationStatus.empty())
        {
            lg2::error("activationStatus was either empty or no match");
            return;
        }
        else if (activationStatus == failedStatus)
        {
            lg2::error("activationStatus found. Failed status.");
            fs::remove_all(usbDir);
            writeFail(bus);
            event.exit(0);
        }
        else if (activationStatus == activeStatus)
        {
            event.exit(0);
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed in getting Activation, ERROR:{ERROR}", "ERROR",
                   e.what());
    }
}

void USBManager::setProgressSRC(
    const std::tuple<std::vector<uint8_t>, std::vector<uint8_t>>& prgCode,
    sdbusplus::bus::bus& bus)
{
    static constexpr auto bootRawProgress =
        "xyz.openbmc_project.State.Boot.Raw";
    static constexpr auto bootRawSetting =
        "/xyz/openbmc_project/state/boot/raw0";
    static constexpr auto dbusProperty = "org.freedesktop.DBus.Properties";
    static constexpr auto rawProperty = "Value";
    static constexpr auto setMethod = "Set";

    try
    {
        auto method = bus.new_method_call(bootRawProgress, bootRawSetting,
                                          dbusProperty, setMethod);
        method.append(bootRawProgress, rawProperty,
                      std::variant<std::tuple<std::vector<uint8_t>,
                                              std::vector<uint8_t>>>(prgCode));
        bus.call(method);
    }
    catch (std::exception& e)
    {
        lg2::error(
            "Failed to call d-bus call to boot progress interface:{ERROR}",
            "ERROR", e);
    }
}

void USBManager::writeProgressSRC(const std::vector<uint8_t>& primArray,
                                  sdbusplus::bus::bus& bus)
{
    const size_t refcodeBegin = 40;
    const size_t refcodeSize = 8;

    // Initialize vector that acts as the secondary progress code which contains
    // the ascii (8-bit) primary code which is displayed on the op panel and the
    // secondary code which contain hex words.
    std::vector<uint8_t> secArr{
        2,  0,  0,  1,  0,  0,  0,  72, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  32, 0,  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32};

    std::copy(secArr.begin(), secArr.begin() + refcodeSize,
              secArr.begin() + refcodeBegin);

    setProgressSRC(std::make_tuple(primArray, secArr), bus);
}

void USBManager::writeSuccess(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> successArray{67, 49, 48, 48, 66, 49, 48, 51};
    writeProgressSRC(successArray, bus);
}

void USBManager::writeSideOneProgress(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> sameSideArray{67, 49, 48, 48, 66, 49, 48, 49};
    writeProgressSRC(sameSideArray, bus);
}

void USBManager::writeSideTwoProgress(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> oppSideArray{67, 49, 48, 48, 66, 49, 48, 50};
    writeProgressSRC(oppSideArray, bus);
}

void USBManager::writeFail(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> failArray{67, 49, 48, 48, 66, 49, 48, 52};
    writeProgressSRC(failArray, bus);
}

#endif // START_UPDATE_DBUS_INTEFACE

} // namespace usb
} // namespace phosphor
