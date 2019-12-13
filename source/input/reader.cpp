#include "reader.h"
#include "devicereader.h"
#include "frameq.h"

Reader::Reader()
{
}

bool Reader::ParseDevicesFromCommandLine(int argc, char** argv)
{
    if(argc < 2)
    {
        std::cout << "Invalid syntax. Try:\n";
        std::cout << "./x265enc --input /home/oosman/Downloads/rgb.data --input-res 700x600 --fps 30 --output /tmp/frames.hevc\n";
        std::cout << "./x265enc --input /home/oosman/Downloads/rgb.data --input-res 700x600 --fps 30 --output udp://127.0.0.1:7878\n";
        return false;
    }
    for(int i = 0; i < argc; ++i)
    {
        if(strlen(argv[i]) < 2)
        {
            std::cout << "Invalid syntax\n";
            return false;
        }
        constexpr char DEVICE[] = "--input";
        constexpr char QUALITY[] = "--quality";
        constexpr char FPS[] = "--fps";

        if(strncmp(argv[i], DEVICE, strlen(DEVICE))==0)
        {
            args.device.Name = argv[++i];
        }
        else if(strncmp(argv[i], QUALITY, strlen(QUALITY))==0)
        {
            args.Quality = std::stoi(argv[++i]);
        }
        else if(strncmp(argv[i], FPS, strlen(FPS))==0)
        {
            args.Fps =argv[++i];
        }
    }
    if(args.device.Name.size() == 0)
    {
        std::cout << "No input specified\n";
        return false;
    }
    return true;
}

bool Reader::ReadYuvFrame(char *frame, uint32_t framesz, int width, int height)
{
    RawImageReader reader;
    if(!reader.ReadAsYuv(args.device.Name.c_str(), width, height))
    {
        std::cout << "Failed to read device\n";
        return false;
    }
    memcpy(frame, reader.YUV, framesz);
    return true;
}
