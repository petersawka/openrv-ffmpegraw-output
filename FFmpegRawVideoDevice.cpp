#include <FFmpegRawOutput/FFmpegRawVideoDevice.h>
#include <FFmpegRawOutput/FFmpegRawModule.h>

#include <TwkGLF/GLFBO.h>
#include <TwkGLF/GL.h>
#include <TwkExc/Exception.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#ifndef PLATFORM_WINDOWS
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace FFmpegRaw
{
    using namespace TwkApp;
    using namespace TwkGLF;

    static const std::vector<RawVideoFormat> videoFormats{
        // 1920×1080
        {1920, 1080, 1.0f, 24.00, 24000, 1000, "1080p 24Hz"},
        {1920, 1080, 1.0f, 29.97, 30000, 1001, "1080p 29.97Hz"},
        {1920, 1080, 1.0f, 30.00, 30000, 1000, "1080p 30Hz"},
        {1920, 1080, 1.0f, 60.00, 60000, 1000, "1080p 60Hz"},
        // DCI 2048×1080
        {2048, 1080, 1.0f, 24.00, 24000, 1000, "DCI 2K 24Hz"},
        {2048, 1080, 1.0f, 29.97, 30000, 1001, "DCI 2K 29.97Hz"},
        {2048, 1080, 1.0f, 30.00, 30000, 1000, "DCI 2K 30Hz"},
        {2048, 1080, 1.0f, 48.00, 48000, 1000, "DCI 2K 48Hz"},
        {2048, 1080, 1.0f, 60.00, 60000, 1000, "DCI 2K 60Hz"},
    };

    static const std::vector<RawDataFormat> dataFormats{
        {VideoDevice::RGBA8,  "rgba",     "8-bit RGBA"},
        {VideoDevice::RGBA16, "rgba64le", "16-bit RGBA (HDR)"},
    };

    struct RawSyncMode { const char* description; };
    static const std::vector<RawSyncMode> syncModes{{"Free Running"}};

    // -----------------------------------------------------------------------

    FFmpegRawVideoDevice::FFmpegRawVideoDevice(FFmpegRawModule* module, const std::string& name)
        : GLBindableVideoDevice(module, name, ImageOutput)
    {}

    FFmpegRawVideoDevice::~FFmpegRawVideoDevice()
    {
        if (m_isOpen) close();
    }

    // --- format accessors ---------------------------------------------------

    size_t FFmpegRawVideoDevice::numVideoFormats() const { return videoFormats.size(); }

    FFmpegRawVideoDevice::VideoFormat FFmpegRawVideoDevice::videoFormatAtIndex(size_t i) const
    {
        const auto& f = videoFormats[i];
        return {size_t(f.width), size_t(f.height), f.pixelAspect, 1.0f, float(f.hertz), f.description};
    }

    void FFmpegRawVideoDevice::setVideoFormat(size_t i)
    {
        m_internalVideoFormat = std::min(i, videoFormats.size() - 1);
    }

    size_t FFmpegRawVideoDevice::currentVideoFormat() const { return m_internalVideoFormat; }

    size_t FFmpegRawVideoDevice::numDataFormats() const { return dataFormats.size(); }

    FFmpegRawVideoDevice::DataFormat FFmpegRawVideoDevice::dataFormatAtIndex(size_t i) const
    {
        return {dataFormats[i].iformat, dataFormats[i].description};
    }

    void FFmpegRawVideoDevice::setDataFormat(size_t i)
    {
        m_internalDataFormat = std::min(i, dataFormats.size() - 1);
    }

    size_t FFmpegRawVideoDevice::currentDataFormat() const { return m_internalDataFormat; }

    size_t FFmpegRawVideoDevice::numSyncModes() const { return syncModes.size(); }

    FFmpegRawVideoDevice::SyncMode FFmpegRawVideoDevice::syncModeAtIndex(size_t i) const
    {
        return {syncModes[i].description};
    }

    void FFmpegRawVideoDevice::setSyncMode(size_t i)
    {
        m_internalSyncMode = std::min(i, syncModes.size() - 1);
    }

    size_t FFmpegRawVideoDevice::currentSyncMode() const { return m_internalSyncMode; }

    FFmpegRawVideoDevice::VideoFormat FFmpegRawVideoDevice::format() const
    {
        const auto& f = videoFormats[m_internalVideoFormat];
        return {size_t(f.width), size_t(f.height), f.pixelAspect, 1.0f, float(f.hertz), f.description};
    }

    FFmpegRawVideoDevice::Timing FFmpegRawVideoDevice::timing() const
    {
        return {float(videoFormats[m_internalVideoFormat].hertz)};
    }

    // --- open / close -------------------------------------------------------

    void FFmpegRawVideoDevice::open(const StringVector& /*args*/)
    {
        if (m_isOpen) return;

        // --- env vars ---
        //   RV_FFMPEG_NUT_OUTPUT  — output file path (default /tmp/rv_output.nut)
        //   RV_FFMPEG_NUT_CODEC   — video codec (default rawvideo; try ffv1 for lossless compression)
        //   RV_FFMPEG_NUT_EXTRA_ARGS — appended verbatim to the ffmpeg command
        //   RV_FFMPEG_BIN         — path to ffmpeg binary
        const char* envBin      = std::getenv("RV_FFMPEG_BIN");
        const char* envOutput   = std::getenv("RV_FFMPEG_NUT_OUTPUT");
        const char* envCodec    = std::getenv("RV_FFMPEG_NUT_CODEC");
        const char* envExtra    = std::getenv("RV_FFMPEG_NUT_EXTRA_ARGS");

        m_ffmpegBin  = envBin    ? envBin    : "ffmpeg";
        m_outputPath = envOutput ? envOutput : "/tmp/rv_output.nut";
        const std::string codec = envCodec ? envCodec : "rawvideo";

        const auto& vf = videoFormats[m_internalVideoFormat];
        const auto& df = dataFormats[m_internalDataFormat];

        m_frameWidth  = size_t(vf.width);
        m_frameHeight = size_t(vf.height);
        const size_t bpp = (df.iformat == VideoDevice::RGBA16) ? 8 : 4;
        m_frameBuffer.resize(m_frameWidth * m_frameHeight * bpp);

        // Build the ffmpeg command.
        // -re paces reads to real-time so the NUT file has correct timestamps
        // even if RV renders faster than the chosen frame rate.
        // -y overwrites an existing output file without prompting.
        std::ostringstream cmd;
        cmd << m_ffmpegBin
            << " -y"
            << " -re"
            << " -f rawvideo"
            << " -pix_fmt " << df.ffmpegPixFmt
            << " -s " << vf.width << "x" << vf.height
            << " -r " << vf.frame_rate_N << "/" << vf.frame_rate_D
            << " -i pipe:0"
            << " -c:v " << codec;

        // For rawvideo the output pix_fmt must be stated explicitly;
        // for compressed codecs (ffv1, etc.) omit it and let the codec decide.
        if (codec == "rawvideo")
        {
            cmd << " -pix_fmt " << df.ffmpegPixFmt;
        }

        cmd << " -f nut"
            << " \"" << m_outputPath << "\"";

        if (envExtra)
        {
            cmd << " " << envExtra;
        }

        const std::string cmdStr = cmd.str();
        std::cout << "FFmpeg-Raw: " << cmdStr << '\n';

#ifndef PLATFORM_WINDOWS
        int pipefd[2];
        if (pipe(pipefd) != 0)
        {
            std::cerr << "FFmpeg-Raw: pipe() failed\n";
            return;
        }

#ifdef F_SETPIPE_SZ
        fcntl(pipefd[1], F_SETPIPE_SZ, 8 * 1024 * 1024);
#endif

        pid_t pid = fork();
        if (pid < 0)
        {
            std::cerr << "FFmpeg-Raw: fork() failed\n";
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            return;
        }

        if (pid == 0)
        {
            setsid();
            dup2(pipefd[0], STDIN_FILENO);
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            execl("/bin/sh", "sh", "-c", cmdStr.c_str(), nullptr);
            _exit(127);
        }

        ::close(pipefd[0]);
        m_ffmpegPid = pid;
        m_pipe = fdopen(pipefd[1], "w");
        if (m_pipe == nullptr)
        {
            std::cerr << "FFmpeg-Raw: fdopen failed\n";
            ::close(pipefd[1]);
            return;
        }
#else
        m_pipe = _popen(cmdStr.c_str(), "wb");
        if (m_pipe == nullptr)
        {
            std::cerr << "FFmpeg-Raw: _popen failed\n";
            return;
        }
#endif

        m_writerStop = false;
        {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            m_queue.clear();
        }
        m_writerThread = std::thread(&FFmpegRawVideoDevice::writerThreadFunc, this);

        m_isOpen = true;
        std::cout << "FFmpeg-Raw: recording to " << m_outputPath << '\n';
    }

    void FFmpegRawVideoDevice::close()
    {
        // Signal writer thread and drain remaining frames before closing pipe.
        if (m_writerThread.joinable())
        {
            {
                std::unique_lock<std::mutex> lk(m_queueMutex);
                m_writerStop = true;
            }
            m_queueCv.notify_one();
            m_writerThread.join();
        }

#ifndef PLATFORM_WINDOWS
        sigset_t newSet, oldSet;
        sigemptyset(&newSet);
        sigaddset(&newSet, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &newSet, &oldSet);
#endif
        if (m_pipe != nullptr)
        {
            fclose(m_pipe);
            m_pipe = nullptr;
        }
#ifndef PLATFORM_WINDOWS
        pthread_sigmask(SIG_SETMASK, &oldSet, nullptr);
        if (m_ffmpegPid > 0)
        {
            waitpid(m_ffmpegPid, nullptr, 0);
            m_ffmpegPid = -1;
        }
#endif

        m_frameBuffer.clear();
        m_frameBuffer.shrink_to_fit();
        m_isOpen = false;
        TwkGLF::GLBindableVideoDevice::close();
    }

    bool FFmpegRawVideoDevice::isOpen() const { return m_isOpen; }

    // --- GL binding ---------------------------------------------------------

    void FFmpegRawVideoDevice::bind(const TwkGLF::GLVideoDevice*) const { resetClock(); }
    void FFmpegRawVideoDevice::bind2(const TwkGLF::GLVideoDevice* d, const TwkGLF::GLVideoDevice*) const { bind(d); }
    void FFmpegRawVideoDevice::unbind() const {}

    // --- writer thread ------------------------------------------------------

    void FFmpegRawVideoDevice::writerThreadFunc()
    {
#ifndef PLATFORM_WINDOWS
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
#endif

        std::vector<uint8_t> localFrame;
        bool pipeBroken = false;

        while (true)
        {
            {
                std::unique_lock<std::mutex> lk(m_queueMutex);
                m_queueCv.wait(lk, [this] { return !m_queue.empty() || m_writerStop; });
                if (m_writerStop && m_queue.empty()) break;
                localFrame = std::move(m_queue.front());
                m_queue.pop_front();
            }

            if (pipeBroken || m_pipe == nullptr || localFrame.empty()) continue;

            const size_t written = std::fwrite(localFrame.data(), 1, localFrame.size(), m_pipe);
            if (written != localFrame.size())
            {
                std::cerr << "FFmpeg-Raw: pipe broken — ffmpeg may have exited\n";
                pipeBroken = true;
            }
            else
            {
                std::fflush(m_pipe);
            }
        }
    }

    // --- frame capture ------------------------------------------------------

    void FFmpegRawVideoDevice::writeFrame(const GLFBO* fbo) const
    {
        fbo->bind();

        const auto& df = dataFormats[m_internalDataFormat];
        const GLenum glFormat = GL_RGBA;
        const GLenum glType   = (df.iformat == VideoDevice::RGBA16) ? GL_UNSIGNED_SHORT
                                                                     : GL_UNSIGNED_BYTE;

        glReadPixels(0, 0,
                     GLsizei(m_frameWidth), GLsizei(m_frameHeight),
                     glFormat, glType,
                     m_frameBuffer.data());

        fbo->endExternalReadback();

        std::unique_lock<std::mutex> lk(m_queueMutex, std::try_to_lock);
        if (!lk.owns_lock()) return;

        std::vector<uint8_t> frame;
        if (m_queue.size() >= kMaxQueueDepth)
        {
            std::cerr << "FFmpeg-Raw: queue full — dropping oldest frame (disk too slow?)\n";
            frame = std::move(m_queue.front());
            m_queue.pop_front();
        }

        if (frame.size() != m_frameBuffer.size())
            frame.resize(m_frameBuffer.size());

        // Flip vertically (GL reads bottom-up, ffmpeg expects top-down).
        const size_t rowBytes = m_frameBuffer.size() / m_frameHeight;
        for (size_t y = 0; y < m_frameHeight; ++y)
        {
            std::memcpy(frame.data() + y * rowBytes,
                        m_frameBuffer.data() + (m_frameHeight - 1 - y) * rowBytes,
                        rowBytes);
        }

        m_queue.push_back(std::move(frame));
        lk.unlock();
        m_queueCv.notify_one();
    }

    void FFmpegRawVideoDevice::transfer(const GLFBO* fbo) const
    {
        if (!m_isOpen || m_pipe == nullptr) return;
        writeFrame(fbo);
        incrementClock();
    }

    void FFmpegRawVideoDevice::transfer2(const GLFBO* fbo1, const GLFBO*) const
    {
        transfer(fbo1);
    }

} // namespace FFmpegRaw
