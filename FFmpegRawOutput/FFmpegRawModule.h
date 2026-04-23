// Copyright (C) 2024  Autodesk, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <TwkApp/VideoModule.h>
#include <string>
#include <vector>

namespace FFmpegRaw
{

    class FFmpegRawModule : public TwkApp::VideoModule
    {
    public:
        FFmpegRawModule();
        virtual ~FFmpegRawModule() final;

        std::string name() const override;
        std::string SDKIdentifier() const override;
        std::string SDKInfo() const override;

        void open() override;
        void close() override;
        bool isOpen() const override;
    };

} // namespace FFmpegRaw
