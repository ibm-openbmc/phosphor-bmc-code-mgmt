#include "config.h"

#include "watch.hpp"

#include "image_manager.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace phosphor
{
namespace software
{
namespace manager
{

PHOSPHOR_LOG2_USING;
using namespace std::string_literals;
namespace fs = std::filesystem;

Watch::Watch(sd_event* loop, std::function<int(std::string&)> imageCallback) :
    imageCallback(std::move(imageCallback))
{
    // Check if IMAGE DIR exists.
    std::error_code ec;
    fs::path imgDirPath(IMG_UPLOAD_DIR);
    if (!fs::is_directory(imgDirPath, ec))
    {
        fs::create_directories(imgDirPath, ec);
    }

    fd = inotify_init1(IN_NONBLOCK);
    if (-1 == fd)
    {
        // Store a copy of errno, because the string creation below will
        // invalidate errno due to one more system calls.
        auto error = errno;
        throw std::runtime_error(
            "inotify_init1 failed, errno="s + std::strerror(error));
    }

    wd = inotify_add_watch(fd, IMG_UPLOAD_DIR, IN_CLOSE_WRITE);
    if (-1 == wd)
    {
        auto error = errno;
        close(fd);
        throw std::runtime_error(
            "inotify_add_watch failed, errno="s + std::strerror(error));
    }

    auto rc = sd_event_add_io(loop, nullptr, fd, EPOLLIN, callback, this);
    if (0 > rc)
    {
        throw std::runtime_error(
            "failed to add to event loop, rc="s + std::strerror(-rc));
    }
}

Watch::~Watch()
{
    if (-1 != fd)
    {
        if (-1 != wd)
        {
            inotify_rm_watch(fd, wd);
        }
        close(fd);
    }
}

int Watch::callback(sd_event_source* /* s */, int fd, uint32_t revents,
                    void* userdata)
{
    if (!(revents & EPOLLIN))
    {
        return 0;
    }

    constexpr auto maxBytes = 1024;
    uint8_t buffer[maxBytes];
    auto bytes = read(fd, buffer, maxBytes);
    if (0 > bytes)
    {
        auto error = errno;
        throw std::runtime_error(
            "failed to read inotify event, errno="s + std::strerror(error));
    }

    auto offset = 0;
    while (offset < bytes)
    {
        auto event = reinterpret_cast<inotify_event*>(&buffer[offset]);
        if ((event->mask & IN_CLOSE_WRITE) && !(event->mask & IN_ISDIR))
        {
            auto tarballPath = std::string{IMG_UPLOAD_DIR} + '/' + event->name;
            auto rc = static_cast<Watch*>(userdata)->imageCallback(tarballPath);
            if (rc < 0)
            {
                error("Error ({RC}) processing image {IMAGE}", "RC", rc,
                      "IMAGE", tarballPath);
            }
        }

        offset += offsetof(inotify_event, name) + event->len;
    }

    return 0;
}

} // namespace manager
} // namespace software
} // namespace phosphor
