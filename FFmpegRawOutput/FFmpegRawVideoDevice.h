// Copyright (C) 2024  Autodesk, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <TwkGLF/GLVideoDevice.h>
#include <TwkGLF/GLFBO.h>
#include <TwkGLF/GL.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef PLATFORM_WINDOWS
#include <sys/types.h>
#endif

namespace FFmpegRaw
{
    class FFmpegRawModule;

    struct RawVideoFormat
    {
        int width{0};
        int height{0};
        float pixelAspect{1.0f};
        double hertz{0.0};
        int frame_rate_N{0};
        int frame_rate_D{0};
        const char* description{nullptr};
    };

    struct RawDataFormat
    {
        TwkApp::VideoDevice::InternalDataFormat iformat{TwkApp::VideoDevice::RGBA8};
        const char* ffmpegPixFmt{nullptr};
        const char* description{nullptr};
    };

    class FFmpegRawVideoDevice : public TwkGLF::GLBindableVideoDevice
    {
    public:
        using GLFBO = TwkGLF::GLFBO;

        FFmpegRawVideoDevice(FFmpegRawModule* module, const std::string& name);
        virtual ~FFmpegRawVideoDevice() final;

        size_t asyncMaxMappedBuffers() const override { return 0; }
        Time deviceLatency() const override { return Time(0); }

        size_t numVideoFormats() const override;
        VideoFormat videoFormatAtIndex(size_t index) const override;
        void setVideoFormat(size_t index) override;
        size_t currentVideoFormat() const override;

        size_t numDataFormats() const override;
        DataFormat dataFormatAtIndex(size_t index) const override;
        void setDataFormat(size_t index) override;
        size_t currentDataFormat() const override;

        size_t numAudioFormats() const override { return 0; }
        AudioFormat audioFormatAtIndex(size_t) const override { return {}; }
        void setAudioFormat(size_t) override {}
        size_t currentAudioFormat() const override { return 0; }

        size_t numSyncModes() const override;
        SyncMode syncModeAtIndex(size_t index) const override;
        void setSyncMode(size_t index) override;
        size_t currentSyncMode() const override;

        size_t numSyncSources() const override { return 0; }
        SyncSource syncSourceAtIndex(size_t) const override { return {}; }
        size_t currentSyncSource() const override { return 0; }

        void open(const StringVector& args) override;
        void close() override;
        bool isOpen() const override;
        void clearCaches() const override {}

        VideoFormat format() const override;
        Timing timing() const override;

        size_t width() const override { return m_frameWidth; }
        size_t height() const override { return m_frameHeight; }

        void bind(const TwkGLF::GLVideoDevice*) const override;
        void bind2(const TwkGLF::GLVideoDevice*, const TwkGLF::GLVideoDevice*) const override;
        void unbind() const override;

        void transfer(const GLFBO*) const override;
        void transfer2(const GLFBO*, const GLFBO*) const override;
        bool willBlockOnTransfer() const override { return false; }
        bool readyForTransfer() const override { return m_isOpen && m_pipe != nullptr; }

        void transferAudio(void*, size_t) const override {}
        void audioFrameSizeSequence(AudioFrameSizeVector& fsizes) const override { fsizes.clear(); }

    private:
        void writeFrame(const GLFBO* fbo) const;
        void writerThreadFunc();

        mutable FILE* m_pipe{nullptr};
#ifndef PLATFORM_WINDOWS
        pid_t m_ffmpegPid{-1};
#endif
        bool   m_isOpen{false};
        size_t m_frameWidth{0};
        size_t m_frameHeight{0};
        size_t m_internalVideoFormat{0};
        size_t m_internalDataFormat{0};
        size_t m_internalSyncMode{0};
        mutable std::vector<uint8_t> m_frameBuffer;
        std::string m_ffmpegBin;
        std::string m_outputPath;

        // 10-frame ring buffer: larger than the streaming plugin because disk
        // I/O can hiccup longer than network I/O. Frames are dropped (with a
        // warning) only when the queue is full to protect render performance.
        static constexpr size_t kMaxQueueDepth = 10;
        mutable std::mutex                       m_queueMutex;
        mutable std::condition_variable          m_queueCv;
        mutable std::deque<std::vector<uint8_t>> m_queue;
        mutable bool                             m_writerStop{false};
        std::thread                              m_writerThread;
    };

} // namespace FFmpegRaw
