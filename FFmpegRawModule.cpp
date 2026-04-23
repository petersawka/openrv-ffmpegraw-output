#include <FFmpegRawOutput/FFmpegRawModule.h>
#include <FFmpegRawOutput/FFmpegRawVideoDevice.h>
#include <TwkExc/Exception.h>

namespace FFmpegRaw
{

    FFmpegRawModule::FFmpegRawModule()
        : VideoModule()
    {
        open();

        if (!isOpen())
        {
            TWK_THROW_EXC_STREAM("Cannot initialize FFmpeg Raw output module");
        }
    }

    FFmpegRawModule::~FFmpegRawModule() { close(); }

    std::string FFmpegRawModule::name() const { return "FFmpeg Raw Output Module"; }

    std::string FFmpegRawModule::SDKIdentifier() const { return "FFmpeg NUT"; }

    std::string FFmpegRawModule::SDKInfo() const { return ""; }

    void FFmpegRawModule::open()
    {
        if (isOpen()) return;

        auto* device = new FFmpegRawVideoDevice(this, "FFmpeg-Raw");
        if (device->numVideoFormats() != 0)
        {
            m_devices.push_back(device);
        }
        else
        {
            delete device;
        }
    }

    void FFmpegRawModule::close()
    {
        for (auto* d : m_devices) delete d;
        m_devices.clear();
    }

    bool FFmpegRawModule::isOpen() const { return !m_devices.empty(); }

} // namespace FFmpegRaw
