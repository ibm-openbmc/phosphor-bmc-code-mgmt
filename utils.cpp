#include "utils.hpp"

#include <unistd.h>

#include <phosphor-logging/lg2.hpp>

namespace utils
{

PHOSPHOR_LOG2_USING;

std::string getService(sdbusplus::bus_t& bus, const std::string& path,
                       const std::string& interface)
{
    auto method = bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                      MAPPER_BUSNAME, "GetObject");

    method.append(path);
    method.append(std::vector<std::string>({interface}));

    std::vector<std::pair<std::string, std::vector<std::string>>> response;

    try
    {
        auto reply = bus.call(method);
        reply.read(response);
        if (response.empty())
        {
            error(
                "Empty response from mapper for getting service name: {PATH} {INTERFACE}",
                "PATH", path, "INTERFACE", interface);
            return std::string{};
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("Error in mapper method call for ({PATH}, {INTERFACE}: {ERROR}",
              "ERROR", e, "PATH", path, "INTERFACE", interface);
        return std::string{};
    }
    return response[0].first;
}

void setProperty(sdbusplus::bus_t& bus, const std::string& objectPath,
                 const std::string& interface, const std::string& propertyName,
                 const PropertyValue& value)
{
    auto service = getService(bus, objectPath, interface);
    if (service.empty())
    {
        return;
    }

    auto method = bus.new_method_call(service.c_str(), objectPath.c_str(),
                                      "org.freedesktop.DBus.Properties", "Set");
    method.append(interface.c_str(), propertyName.c_str(), value);

    bus.call_noreply(method);
}

void mergeFiles(const std::vector<std::string>& srcFiles,
                const std::string& dstFile)
{
    std::ofstream outFile(dstFile, std::ios::out);
    for (const auto& file : srcFiles)
    {
        std::ifstream inFile;
        inFile.open(file, std::ios_base::in);
        if (!inFile)
        {
            continue;
        }

        inFile.peek();
        if (inFile.eof())
        {
            inFile.close();
            continue;
        }

        outFile << inFile.rdbuf();
        inFile.close();
    }
    outFile.close();
}

void createBmcDump(sdbusplus::bus::bus& bus)
{
    auto method = bus.new_method_call(
        "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump/bmc",
        "xyz.openbmc_project.Dump.Create", "CreateDump");
    method.append(
        std::vector<
            std::pair<std::string, std::variant<std::string, uint64_t>>>());
    try
    {
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Failed to create BMC dump, exception:{ERROR}", "ERROR", e);
    }
}

#ifdef USB_DEFINED
void setProgressSRC(const std::tuple<uint64_t, std::vector<uint8_t>>& prgCode,
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
        method.append(
            bootRawProgress, rawProperty,
            std::variant<std::tuple<uint64_t, std::vector<uint8_t>>>(prgCode));
        bus.call(method);
    }
    catch (std::exception& e)
    {
        error("Failed to call d-bus call to boot progress interface:{ERROR}",
              "ERROR", e);
    }
}

void writeProgressSRC(const std::vector<uint8_t>& primArray,
                      sdbusplus::bus::bus& bus)
{
    uint64_t primaryCode = 0;
    // Initialize vector that acts as the secondary progress code which contains
    // the ascii (8-bit) primary code which is displayed on the op panel and the
    // secondary code which contain hex words.
    std::vector<uint8_t> secArr{
        2,  0,  0,  1,  0,  0,  0,  72, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  32, 0,  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32};

    for (int i = 0; i < 8; i++)
    {
        secArr[40 + i] = secArr[i];
        primaryCode |= (uint64_t)primArray[i] << 8 * i;
    }

    setProgressSRC(std::make_tuple(primaryCode, secArr), bus);
}

void writeSuccess(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> successArray{67, 49, 48, 48, 66, 49, 48, 51};
    utils::writeProgressSRC(successArray, bus);
}

void writeSideOneProgress(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> sameSideArray{67, 49, 48, 48, 66, 49, 48, 49};
    utils::writeProgressSRC(sameSideArray, bus);
}

void writeSideTwoProgress(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> oppSideArray{67, 49, 48, 48, 66, 49, 48, 50};
    utils::writeProgressSRC(oppSideArray, bus);
}

void writeFail(sdbusplus::bus::bus& bus)
{
    std::vector<uint8_t> failArray{67, 49, 48, 48, 66, 49, 48, 52};
    utils::writeProgressSRC(failArray, bus);
}
#endif
namespace internal
{

/* @brief Helper function to build a string from command arguments */
static std::string buildCommandStr(const char* name, char** args)
{
    std::string command = name;
    for (int i = 0; args[i]; i++)
    {
        command += " ";
        command += args[i];
    }
    return command;
}

int executeCmd(const char* path, char** args)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execv(path, args);

        // execv only retruns on err
        auto err = errno;
        auto command = buildCommandStr(path, args);
        error("Failed ({ERRNO}) to execute command: {COMMAND}", "ERRNO", err,
              "COMMAND", command);
        return -1;
    }
    else if (pid > 0)
    {
        int status;
        if (waitpid(pid, &status, 0) < 0)
        {
            error("Error ({ERRNO}) during waitpid.", "ERRNO", errno);
            return -1;
        }
        else if (WEXITSTATUS(status) != 0)
        {
            auto command = buildCommandStr(path, args);
            error("Error ({STATUS}) occurred when executing command: {COMMAND}",
                  "STATUS", status, "COMMAND", command);
            return -1;
        }
    }
    else
    {
        error("Error ({ERRNO}) during fork.", "ERRNO", errno);
        return -1;
    }

    return 0;
}

} // namespace internal

} // namespace utils
