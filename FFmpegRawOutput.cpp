// Copyright (C) 2024  Autodesk, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <FFmpegRawOutput/FFmpegRawModule.h>

extern "C"
{
#ifdef PLATFORM_WINDOWS
    __declspec(dllexport) TwkApp::VideoModule* output_module_create(float, int);
    __declspec(dllexport) void output_module_destroy(TwkApp::VideoModule*);
#endif

    TwkApp::VideoModule* output_module_create(float /*version*/, int index)
    {
        try
        {
            if (index == 0) return new FFmpegRaw::FFmpegRawModule();
        }
        catch (...) {}
        return nullptr;
    }

    void output_module_destroy(TwkApp::VideoModule* m) { delete m; }

} // extern "C"
