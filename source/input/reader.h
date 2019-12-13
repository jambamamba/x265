#pragma once

#include <string.h>
#include <iostream>
#include <memory>
#include <vector>

#include "frameq.h"

class Reader
{
public:
    Reader();

    struct Device {
        std::string Name;
    };

    struct CommandLineArs
    {
        Device device;
        std::string Fps;
        int Quality = 80;
    };

    bool ParseDevicesFromCommandLine(int argc, char** argv);
    bool ReadYuvFrame(char *frame, uint32_t framesz, int width, int height);

    FrameQueue queue;
    CommandLineArs args;
};
