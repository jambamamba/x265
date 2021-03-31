/*****************************************************************************
 * Copyright (C) 2013-2017 MulticoreWare, Inc
 *
 * Authors: Steve Borho <steve@borho.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#if _MSC_VER
#pragma warning(disable: 4127) // conditional expression is constant, yes I know
#endif

#include "x265.h"
#include "x265cli.h"

#include "input/input.h"
#include "output/output.h"
#include "output/reconplay.h"
#include "svt.h"

#if HAVE_VLD
/* Visual Leak Detector */
#include <vld.h>
#endif

#include <atomic>
#include <functional>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <ostream>
#include <fstream>
#include <queue>

#include "input/frameq.h"
#include "input/reader.h"

#define CONSOLE_TITLE_SIZE 200
#ifdef _WIN32
#include <windows.h>
#define SetThreadExecutionState(es)
static char orgConsoleTitle[CONSOLE_TITLE_SIZE] = "";
#else
#define GetConsoleTitle(t, n)
#define SetConsoleTitle(t)
#define SetThreadExecutionState(es)
#endif

using namespace X265_NS;

/* Ctrl-C handler */
static volatile sig_atomic_t b_ctrl_c /* = 0 */;
static void sigint_handler(int)
{
    b_ctrl_c = 1;
}
#define START_CODE 0x00000001
#define START_CODE_BYTES 4

struct CLIOptions
{
    InputFile* input;
    ReconFile* recon;
    OutputFile* output;
    FILE*       qpfile;
    FILE*       zoneFile;
    FILE*    dolbyVisionRpu;    /* File containing Dolby Vision BL RPU metadata */
    const char* reconPlayCmd;
    const x265_api* api;
    x265_param* param;
    x265_vmaf_data* vmafData;
    bool bProgress;
    bool bForceY4m;
    bool bDither;
    uint32_t seek;              // number of frames to skip from the beginning
    uint32_t framesToBeEncoded; // number of frames to encode
    uint64_t totalbytes;
    int64_t startTime;
    int64_t prevUpdateTime;

    /* in microseconds */
    static const int UPDATE_INTERVAL = 250000;

    CLIOptions()
    {
        input = NULL;
        recon = NULL;
        output = NULL;
        qpfile = NULL;
        zoneFile = NULL;
        dolbyVisionRpu = NULL;
        reconPlayCmd = NULL;
        api = NULL;
        param = NULL;
        vmafData = NULL;
        framesToBeEncoded = seek = 0;
        totalbytes = 0;
        bProgress = true;
        bForceY4m = false;
        startTime = x265_mdate();
        prevUpdateTime = 0;
        bDither = false;
    }

    void destroy();
    void printStatus(uint32_t frameNum);
    bool parse(int argc, char **argv, Reader *reader, std::function<int(const char *data, ssize_t bytes)> callback);
    bool parseZoneParam(int argc, char **argv, x265_param* globalParam, int zonefileCount);
    bool parseQPFile(x265_picture &pic_org);
    bool parseZoneFile();
};

void CLIOptions::destroy()
{
    if (input)
        input->release();
    input = NULL;
    if (recon)
        recon->release();
    recon = NULL;
    if (qpfile)
        fclose(qpfile);
    qpfile = NULL;
    if (zoneFile)
        fclose(zoneFile);
    zoneFile = NULL;
    if (dolbyVisionRpu)
        fclose(dolbyVisionRpu);
    dolbyVisionRpu = NULL;
    if (output)
        output->release();
    output = NULL;
}

void CLIOptions::printStatus(uint32_t frameNum)
{
    char buf[200];
    int64_t time = x265_mdate();

    if (!bProgress || !frameNum || (prevUpdateTime && time - prevUpdateTime < UPDATE_INTERVAL))
        return;

    int64_t elapsed = time - startTime;
    double fps = elapsed > 0 ? frameNum * 1000000. / elapsed : 0;
    float bitrate = 0.008f * totalbytes * (param->fpsNum / param->fpsDenom) / ((float)frameNum);
    if (framesToBeEncoded)
    {
        int eta = (int)(elapsed * (framesToBeEncoded - frameNum) / ((int64_t)frameNum * 1000000));
        sprintf(buf, "x265 [%.1f%%] %d/%d frames, %.2f fps, %.2f kb/s, eta %d:%02d:%02d",
            100. * frameNum / (param->chunkEnd ? param->chunkEnd : param->totalFrames), frameNum, (param->chunkEnd ? param->chunkEnd : param->totalFrames), fps, bitrate,
                eta / 3600, (eta / 60) % 60, eta % 60);
    }
    else
        sprintf(buf, "x265 %d frames: %.2f fps, %.2f kb/s", frameNum, fps, bitrate);

    fprintf(stderr, "%s  \r", buf + 5);
    SetConsoleTitle(buf);
    fflush(stderr); // needed in windows
    prevUpdateTime = time;
}

bool CLIOptions::parseZoneParam(int argc, char **argv, x265_param* globalParam, int zonefileCount)
{
    bool bError = false;
    int bShowHelp = false;
    int outputBitDepth = 0;
    const char *profile = NULL;

    /* Presets are applied before all other options. */
    for (optind = 0;;)
    {
        int c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1)
            break;
        else if (c == 'D')
            outputBitDepth = atoi(optarg);
        else if (c == 'P')
            profile = optarg;
        else if (c == '?')
            bShowHelp = true;
    }

    if (!outputBitDepth && profile)
    {
        /* try to derive the output bit depth from the requested profile */
        if (strstr(profile, "10"))
            outputBitDepth = 10;
        else if (strstr(profile, "12"))
            outputBitDepth = 12;
        else
            outputBitDepth = 8;
    }

    api = x265_api_get(outputBitDepth);
    if (!api)
    {
        x265_log(NULL, X265_LOG_WARNING, "falling back to default bit-depth\n");
        api = x265_api_get(0);
    }

    if (bShowHelp)
    {
        printVersion(globalParam, api);
        showHelp(globalParam);
    }

    globalParam->rc.zones[zonefileCount].zoneParam = api->param_alloc();
    if (!globalParam->rc.zones[zonefileCount].zoneParam)
    {
        x265_log(NULL, X265_LOG_ERROR, "param alloc failed\n");
        return true;
    }

    memcpy(globalParam->rc.zones[zonefileCount].zoneParam, globalParam, sizeof(x265_param));

    for (optind = 0;;)
    {
        int long_options_index = -1;
        int c = getopt_long(argc, argv, short_options, long_options, &long_options_index);
        if (c == -1)
            break;

        if (long_options_index < 0 && c > 0)
        {
            for (size_t i = 0; i < sizeof(long_options) / sizeof(long_options[0]); i++)
            {
                if (long_options[i].val == c)
                {
                    long_options_index = (int)i;
                    break;
                }
            }

            if (long_options_index < 0)
            {
                /* getopt_long might have already printed an error message */
                if (c != 63)
                    x265_log(NULL, X265_LOG_WARNING, "internal error: short option '%c' has no long option\n", c);
                return true;
            }
        }
        if (long_options_index < 0)
        {
            x265_log(NULL, X265_LOG_WARNING, "short option '%c' unrecognized\n", c);
            return true;
        }

        bError |= !!api->zone_param_parse(globalParam->rc.zones[zonefileCount].zoneParam, long_options[long_options_index].name, optarg);

        if (bError)
        {
            const char *name = long_options_index > 0 ? long_options[long_options_index].name : argv[optind - 2];
            x265_log(NULL, X265_LOG_ERROR, "invalid argument: %s = %s\n", name, optarg);
            return true;
        }
    }

    if (optind < argc)
    {
        x265_log(param, X265_LOG_WARNING, "extra unused command arguments given <%s>\n", argv[optind]);
        return true;
    }
    return false;
}

bool CLIOptions::parse(int argc, char **argv, Reader *reader, std::function<int(const char *data, ssize_t bytes)> callback)
{
    bool bError = false;
    int bShowHelp = false;
    int inputBitDepth = 8;
    int outputBitDepth = 0;
    int reconFileBitDepth = 0;
    const char *inputfn = NULL;
    const char *reconfn = NULL;
    const char *outputfn = NULL;
    const char *preset = NULL;
    const char *tune = NULL;
    const char *profile = NULL;
    int svtEnabled = 0;

    if (argc <= 1)
    {
        x265_log(NULL, X265_LOG_ERROR, "No input file. Run x265 --help for a list of options.\n");
        return true;
    }

    /* Presets are applied before all other options. */
    for (optind = 0;; )
    {
        int optionsIndex = -1;
        int c = getopt_long(argc, argv, short_options, long_options, &optionsIndex);
        if (c == -1)
            break;
        else if (c == 'p')
            preset = optarg;
        else if (c == 't')
            tune = optarg;
        else if (c == 'D')
            outputBitDepth = atoi(optarg);
        else if (c == 'P')
            profile = optarg;
        else if (c == '?')
            bShowHelp = true;
        else if (!c && !strcmp(long_options[optionsIndex].name, "svt"))
            svtEnabled = 1;
    }

    if (!outputBitDepth && profile)
    {
        /* try to derive the output bit depth from the requested profile */
        if (strstr(profile, "10"))
            outputBitDepth = 10;
        else if (strstr(profile, "12"))
            outputBitDepth = 12;
        else
            outputBitDepth = 8;
    }

    api = x265_api_get(outputBitDepth);
    if (!api)
    {
        x265_log(NULL, X265_LOG_WARNING, "falling back to default bit-depth\n");
        api = x265_api_get(0);
    }

    param = api->param_alloc();
    if (!param)
    {
        x265_log(NULL, X265_LOG_ERROR, "param alloc failed\n");
        return true;
    }
#if ENABLE_LIBVMAF
    vmafData = (x265_vmaf_data*)x265_malloc(sizeof(x265_vmaf_data));
    if(!vmafData)
    {
        x265_log(NULL, X265_LOG_ERROR, "vmaf data alloc failed\n");
        return true;
    }
#endif

    if (api->param_default_preset(param, preset, tune) < 0)
    {
        x265_log(NULL, X265_LOG_ERROR, "preset or tune unrecognized\n");
        return true;
    }

    if (bShowHelp)
    {
        printVersion(param, api);
        showHelp(param);
    }

    //Set enable SVT-HEVC encoder first if found in the command line
    if (svtEnabled) api->param_parse(param, "svt", NULL);

    for (optind = 0;; )
    {
        int long_options_index = -1;
        int c = getopt_long(argc, argv, short_options, long_options, &long_options_index);
        if (c == -1)
            break;

        switch (c)
        {
        case 'h':
            printVersion(param, api);
            showHelp(param);
            break;

        case 'V':
            printVersion(param, api);
            x265_report_simd(param);
            exit(0);

        default:
            if (long_options_index < 0 && c > 0)
            {
                for (size_t i = 0; i < sizeof(long_options) / sizeof(long_options[0]); i++)
                {
                    if (long_options[i].val == c)
                    {
                        long_options_index = (int)i;
                        break;
                    }
                }

                if (long_options_index < 0)
                {
                    /* getopt_long might have already printed an error message */
                    if (c != 63)
                        x265_log(NULL, X265_LOG_WARNING, "internal error: short option '%c' has no long option\n", c);
                    return true;
                }
            }
            if (long_options_index < 0)
            {
                x265_log(NULL, X265_LOG_WARNING, "short option '%c' unrecognized\n", c);
                return true;
            }
#define OPT(longname) \
    else if (!strcmp(long_options[long_options_index].name, longname))
#define OPT2(name1, name2) \
    else if (!strcmp(long_options[long_options_index].name, name1) || \
             !strcmp(long_options[long_options_index].name, name2))

            if (0) ;
            OPT2("frame-skip", "seek") this->seek = (uint32_t)x265_atoi(optarg, bError);
            OPT("frames") this->framesToBeEncoded = (uint32_t)x265_atoi(optarg, bError);
            OPT("no-progress") this->bProgress = false;
            OPT("output") outputfn = optarg;
            OPT("input") inputfn = optarg;
            OPT("recon") reconfn = optarg;
            OPT("input-depth") inputBitDepth = (uint32_t)x265_atoi(optarg, bError);
            OPT("dither") this->bDither = true;
            OPT("recon-depth") reconFileBitDepth = (uint32_t)x265_atoi(optarg, bError);
            OPT("y4m") this->bForceY4m = true;
            OPT("profile") /* handled above */;
            OPT("preset")  /* handled above */;
            OPT("tune")    /* handled above */;
            OPT("output-depth")   /* handled above */;
            OPT("recon-y4m-exec") reconPlayCmd = optarg;
            OPT("svt")    /* handled above */;
            OPT("qpfile")
            {
                this->qpfile = x265_fopen(optarg, "rb");
                if (!this->qpfile)
                    x265_log_file(param, X265_LOG_ERROR, "%s qpfile not found or error in opening qp file\n", optarg);
            }
            OPT("dolby-vision-rpu")
            {
                this->dolbyVisionRpu = x265_fopen(optarg, "rb");
                if (!this->dolbyVisionRpu)
                {
                    x265_log_file(param, X265_LOG_ERROR, "Dolby Vision RPU metadata file %s not found or error in opening file\n", optarg);
                    return true;
                }
            }
            OPT("zonefile")
            {
                this->zoneFile = x265_fopen(optarg, "rb");
                if (!this->zoneFile)
                    x265_log_file(param, X265_LOG_ERROR, "%s zone file not found or error in opening zone file\n", optarg);
            }
            OPT("fullhelp")
            {
                param->logLevel = X265_LOG_FULL;
                printVersion(param, api);
                showHelp(param);
                break;
            }
            else
                bError |= !!api->param_parse(param, long_options[long_options_index].name, optarg);
            if (bError)
            {
                const char *name = long_options_index > 0 ? long_options[long_options_index].name : argv[optind - 2];
                x265_log(NULL, X265_LOG_ERROR, "invalid argument: %s = %s\n", name, optarg);
                return true;
            }
#undef OPT
        }
    }

    if (optind < argc && !inputfn)
        inputfn = argv[optind++];
    if (optind < argc && !outputfn)
        outputfn = argv[optind++];
    if (optind < argc)
    {
        x265_log(param, X265_LOG_WARNING, "extra unused command arguments given <%s>\n", argv[optind]);
        return true;
    }

    if (argc <= 1)
    {
        api->param_default(param);
        printVersion(param, api);
        showHelp(param);
    }

    if (!inputfn || !outputfn)
    {
        x265_log(param, X265_LOG_ERROR, "input or output file not specified, try --help for help\n");
        return true;
    }

    if (param->internalBitDepth != api->bit_depth)
    {
        x265_log(param, X265_LOG_ERROR, "Only bit depths of %d are supported in this build\n", api->bit_depth);
        return true;
    }

#ifdef SVT_HEVC
    if (svtEnabled)
    {
        EB_H265_ENC_CONFIGURATION* svtParam = (EB_H265_ENC_CONFIGURATION*)param->svtHevcParam;
        param->sourceWidth = svtParam->sourceWidth;
        param->sourceHeight = svtParam->sourceHeight;
        param->fpsNum = svtParam->frameRateNumerator;
        param->fpsDenom = svtParam->frameRateDenominator;
        svtParam->encoderBitDepth = inputBitDepth;
    }
#endif

    InputFileInfo info;
    info.filename = inputfn;
    info.depth = inputBitDepth;
    info.csp = param->internalCsp;
    info.width = param->sourceWidth;
    info.height = param->sourceHeight;
    info.fpsNum = param->fpsNum;
    info.fpsDenom = param->fpsDenom;
    info.sarWidth = param->vui.sarWidth;
    info.sarHeight = param->vui.sarHeight;
    info.skipFrames = seek;
    info.frameCount = 0;
    info.reader = reader;
    getParamAspectRatio(param, info.sarWidth, info.sarHeight);

    reader->args.device.Name = inputfn;

    this->input = InputFile::open(info, this->bForceY4m);
    if (!reader && (!this->input || this->input->isFail()))
    {
        x265_log_file(param, X265_LOG_ERROR, "unable to open input file <%s>\n", inputfn);
        return true;
    }

    if (info.depth < 8 || info.depth > 16)
    {
        x265_log(param, X265_LOG_ERROR, "Input bit depth (%d) must be between 8 and 16\n", inputBitDepth);
        return true;
    }

    /* Unconditionally accept height/width/csp/bitDepth from file info */
    param->sourceWidth = info.width;
    param->sourceHeight = info.height;
    param->internalCsp = info.csp;
    param->sourceBitDepth = info.depth;

    /* Accept fps and sar from file info if not specified by user */
    if (param->fpsDenom == 0 || param->fpsNum == 0)
    {
        param->fpsDenom = info.fpsDenom;
        param->fpsNum = info.fpsNum;
    }
    if (!param->vui.aspectRatioIdc && info.sarWidth && info.sarHeight)
        setParamAspectRatio(param, info.sarWidth, info.sarHeight);
    if (this->framesToBeEncoded == 0 && info.frameCount > (int)seek)
        this->framesToBeEncoded = info.frameCount - seek;
    param->totalFrames = this->framesToBeEncoded;

#ifdef SVT_HEVC
    if (svtEnabled)
    {
        EB_H265_ENC_CONFIGURATION* svtParam = (EB_H265_ENC_CONFIGURATION*)param->svtHevcParam;
        svtParam->sourceWidth = param->sourceWidth;
        svtParam->sourceHeight = param->sourceHeight;
        svtParam->frameRateNumerator = param->fpsNum;
        svtParam->frameRateDenominator = param->fpsDenom;
        svtParam->framesToBeEncoded = param->totalFrames;
		svtParam->encoderColorFormat = (EB_COLOR_FORMAT)param->internalCsp;
    }
#endif
    
    /* Force CFR until we have support for VFR */
    info.timebaseNum = param->fpsDenom;
    info.timebaseDenom = param->fpsNum;

    if (param->bField && param->interlaceMode)
    {   // Field FPS
        param->fpsNum *= 2;
        // Field height
        param->sourceHeight = param->sourceHeight >> 1;
        // Number of fields to encode
        param->totalFrames *= 2;
    }

    if (api->param_apply_profile(param, profile))
        return true;

    if (param->logLevel >= X265_LOG_INFO)
    {
        char buf[128];
        int p = sprintf(buf, "%dx%d fps %d/%d %sp%d", param->sourceWidth, param->sourceHeight,
                        param->fpsNum, param->fpsDenom, x265_source_csp_names[param->internalCsp], info.depth);

        int width, height;
        getParamAspectRatio(param, width, height);
        if (width && height)
            p += sprintf(buf + p, " sar %d:%d", width, height);

        if (framesToBeEncoded <= 0 || info.frameCount <= 0)
            strcpy(buf + p, " unknown frame count");
        else
            sprintf(buf + p, " frames %u - %d of %d", this->seek, this->seek + this->framesToBeEncoded - 1, info.frameCount);

        general_log(param, input->getName(), X265_LOG_INFO, "%s\n", buf);
    }

    this->input->startReader();

    if (reconfn)
    {
        if (reconFileBitDepth == 0)
            reconFileBitDepth = param->internalBitDepth;
        this->recon = ReconFile::open(reconfn, param->sourceWidth, param->sourceHeight, reconFileBitDepth,
                                      param->fpsNum, param->fpsDenom, param->internalCsp);
        if (this->recon->isFail())
        {
            x265_log(param, X265_LOG_WARNING, "unable to write reconstructed outputs file\n");
            this->recon->release();
            this->recon = 0;
        }
        else
            general_log(param, this->recon->getName(), X265_LOG_INFO,
                    "reconstructed images %dx%d fps %d/%d %s\n",
                    param->sourceWidth, param->sourceHeight, param->fpsNum, param->fpsDenom,
                    x265_source_csp_names[param->internalCsp]);
    }
#if ENABLE_LIBVMAF
    if (!reconfn)
    {
        x265_log(param, X265_LOG_ERROR, "recon file must be specified to get VMAF score, try --help for help\n");
        return true;
    }
    const char *str = strrchr(info.filename, '.');

    if (!strcmp(str, ".y4m"))
    {
        x265_log(param, X265_LOG_ERROR, "VMAF supports YUV file format only.\n");
        return true; 
    }
    if(param->internalCsp == X265_CSP_I420 || param->internalCsp == X265_CSP_I422 || param->internalCsp == X265_CSP_I444)
    {
        vmafData->reference_file = x265_fopen(inputfn, "rb");
        vmafData->distorted_file = x265_fopen(reconfn, "rb");
    }
    else
    {
        x265_log(param, X265_LOG_ERROR, "VMAF will support only yuv420p, yu422p, yu444p, yuv420p10le, yuv422p10le, yuv444p10le formats.\n");
        return true;
    }
#endif
    this->output = OutputFile::open(outputfn, info, callback);
    if (this->output->isFail())
    {
        x265_log_file(param, X265_LOG_ERROR, "failed to open output file <%s> for writing\n", outputfn);
        return true;
    }
    general_log_file(param, this->output->getName(), X265_LOG_INFO, "output file: %s\n", outputfn);
    return false;
}

bool CLIOptions::parseQPFile(x265_picture &pic_org)
{
    int32_t num = -1, qp, ret;
    char type;
    uint32_t filePos;
    pic_org.forceqp = 0;
    pic_org.sliceType = X265_TYPE_AUTO;
    while (num < pic_org.poc)
    {
        filePos = ftell(qpfile);
        qp = -1;
        ret = fscanf(qpfile, "%d %c%*[ \t]%d\n", &num, &type, &qp);

        if (num > pic_org.poc || ret == EOF)
        {
            fseek(qpfile, filePos, SEEK_SET);
            break;
        }
        if (num < pic_org.poc && ret >= 2)
            continue;
        if (ret == 3 && qp >= 0)
            pic_org.forceqp = qp + 1;
        if (type == 'I') pic_org.sliceType = X265_TYPE_IDR;
        else if (type == 'i') pic_org.sliceType = X265_TYPE_I;
        else if (type == 'K') pic_org.sliceType = param->bOpenGOP ? X265_TYPE_I : X265_TYPE_IDR;
        else if (type == 'P') pic_org.sliceType = X265_TYPE_P;
        else if (type == 'B') pic_org.sliceType = X265_TYPE_BREF;
        else if (type == 'b') pic_org.sliceType = X265_TYPE_B;
        else ret = 0;
        if (ret < 2 || qp < -1 || qp > 51)
            return 0;
    }
    return 1;
}

bool CLIOptions::parseZoneFile()
{
    char line[256];
    char* argLine;
    param->rc.zonefileCount = 0;

    while (fgets(line, sizeof(line), zoneFile))
    {
        if (!((*line == '#') || (strcmp(line, "\r\n") == 0)))
            param->rc.zonefileCount++;
    }

    rewind(zoneFile);
    param->rc.zones = X265_MALLOC(x265_zone, param->rc.zonefileCount);
    for (int i = 0; i < param->rc.zonefileCount; i++)
    {
        while (fgets(line, sizeof(line), zoneFile))
        {
            if (*line == '#' || (strcmp(line, "\r\n") == 0))
                continue;
            param->rc.zones[i].zoneParam = X265_MALLOC(x265_param, 1);
            int index = (int)strcspn(line, "\r\n");
            line[index] = '\0';
            argLine = line;
            while (isspace((unsigned char)*argLine)) argLine++;
            char* start = strchr(argLine, ' ');
            start++;
            param->rc.zones[i].startFrame = atoi(argLine);
            int argCount = 0;
            char **args = (char**)malloc(256 * sizeof(char *));
            // Adding a dummy string to avoid file parsing error
            args[argCount++] = (char *)"x265";
            char* token = strtok(start, " ");
            while (token) 
            {
                args[argCount++] = token;
                token = strtok(NULL, " ");
            }
            args[argCount] = NULL;
            CLIOptions cliopt;
            if (cliopt.parseZoneParam(argCount, args,param, i))
            {
                cliopt.destroy();
                if (cliopt.api)
                    cliopt.api->param_free(cliopt.param);
                exit(1);
            }
            break;
        }
    }
    return 1;
}

#ifdef _WIN32
/* Copy of x264 code, which allows for Unicode characters in the command line.
 * Retrieve command line arguments as UTF-8. */
static int get_argv_utf8(int *argc_ptr, char ***argv_ptr)
{
    int ret = 0;
    wchar_t **argv_utf16 = CommandLineToArgvW(GetCommandLineW(), argc_ptr);
    if (argv_utf16)
    {
        int argc = *argc_ptr;
        int offset = (argc + 1) * sizeof(char*);
        int size = offset;

        for (int i = 0; i < argc; i++)
            size += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1, NULL, 0, NULL, NULL);

        char **argv = *argv_ptr = (char**)malloc(size);
        if (argv)
        {
            for (int i = 0; i < argc; i++)
            {
                argv[i] = (char*)argv + offset;
                offset += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1, argv[i], size - offset, NULL, NULL);
            }
            argv[argc] = NULL;
            ret = 1;
        }
        LocalFree(argv_utf16);
    }
    return ret;
}
#endif

/* Parse the RPU file and extract the RPU corresponding to the current picture 
 * and fill the rpu field of the input picture */
static int rpuParser(x265_picture * pic, FILE * ptr)
{
    uint8_t byteVal;
    uint32_t code = 0;
    int bytesRead = 0;
    pic->rpu.payloadSize = 0;

    if (!pic->pts)
    {
        while (bytesRead++ < 4 && fread(&byteVal, sizeof(uint8_t), 1, ptr))
            code = (code << 8) | byteVal;
      
        if (code != START_CODE)
        {
            x265_log(NULL, X265_LOG_ERROR, "Invalid Dolby Vision RPU startcode in POC %d\n", pic->pts);
            return 1;
        }
    } 

    bytesRead = 0;
    while (fread(&byteVal, sizeof(uint8_t), 1, ptr))
    {
        code = (code << 8) | byteVal;
        if (bytesRead++ < 3)
            continue;
        if (bytesRead >= 1024)
        {
            x265_log(NULL, X265_LOG_ERROR, "Invalid Dolby Vision RPU size in POC %d\n", pic->pts);
            return 1;
        }
        
        if (code != START_CODE)
            pic->rpu.payload[pic->rpu.payloadSize++] = (code >> (3 * 8)) & 0xFF;
        else
            return 0;       
    }

    int ShiftBytes = START_CODE_BYTES - (bytesRead - pic->rpu.payloadSize);
    int bytesLeft = bytesRead - pic->rpu.payloadSize;
    code = (code << ShiftBytes * 8);
    for (int i = 0; i < bytesLeft; i++)
    {
        pic->rpu.payload[pic->rpu.payloadSize++] = (code >> (3 * 8)) & 0xFF;
        code = (code << 8);
    }
    if (!pic->rpu.payloadSize)
        x265_log(NULL, X265_LOG_WARNING, "Dolby Vision RPU not found for POC %d\n", pic->pts);
    return 0;
}


/* CLI return codes:
 *
 * 0 - encode successful
 * 1 - unable to parse command line
 * 2 - unable to open encoder
 * 3 - unable to generate stream headers
 * 4 - encoder abort */

int x265main(int argc,
             char **argv,
             std::function<int(char **data, ssize_t *bytes, int width, int height)> readRgb888,
             std::function<int(const char *data, ssize_t bytes)> writeEncodedFrame,
             std::atomic<bool> &killed
             )//--input /dev/screen --input-res 1920x1080 --fps 30 --output udp://127.0.0.1:7878
{
#if HAVE_VLD
    // This uses Microsoft's proprietary WCHAR type, but this only builds on Windows to start with
    VLDSetReportOptions(VLD_OPT_REPORT_TO_DEBUGGER | VLD_OPT_REPORT_TO_FILE, L"x265_leaks.txt");
#endif
    PROFILE_INIT();
    THREAD_NAME("API", 0);

    /////osm
    Reader reader(readRgb888);
    if(!reader.ParseDevicesFromCommandLine(argc, argv))
    {
        return -1;
    }
    //////

    GetConsoleTitle(orgConsoleTitle, CONSOLE_TITLE_SIZE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
#if _WIN32
    char** orgArgv = argv;
    get_argv_utf8(&argc, &argv);
#endif

    ReconPlay* reconPlay = NULL;
    CLIOptions cliopt;

    if (cliopt.parse(argc, argv, &reader, writeEncodedFrame))
    {
        cliopt.destroy();
        if (cliopt.api)
            cliopt.api->param_free(cliopt.param);
        exit(1);
    }

    x265_param* param = cliopt.param;
    const x265_api* api = cliopt.api;
#if ENABLE_LIBVMAF
    x265_vmaf_data* vmafdata = cliopt.vmafData;
#endif
    /* This allows muxers to modify bitstream format */
    cliopt.output->setParam(param);

    if (cliopt.reconPlayCmd)
        reconPlay = new ReconPlay(cliopt.reconPlayCmd, *param);

    if (cliopt.zoneFile)
    {
        if (!cliopt.parseZoneFile())
        {
            x265_log(NULL, X265_LOG_ERROR, "Unable to parse zonefile\n");
            fclose(cliopt.zoneFile);
            cliopt.zoneFile = NULL;
        }
    }

    /* note: we could try to acquire a different libx265 API here based on
     * the profile found during option parsing, but it must be done before
     * opening an encoder */

    x265_encoder *encoder = api->encoder_open(param);
    if (!encoder)
    {
        x265_log(param, X265_LOG_ERROR, "failed to open encoder\n");
        cliopt.destroy();
        api->param_free(param);
        api->cleanup();
        exit(2);
    }

    /* get the encoder parameters post-initialization */
    api->encoder_parameters(encoder, param);

     /* Control-C handler */
    if (signal(SIGINT, sigint_handler) == SIG_ERR)
        x265_log(param, X265_LOG_ERROR, "Unable to register CTRL+C handler: %s\n", strerror(errno));

    x265_picture pic_orig, pic_out;
    x265_picture *pic_in = &pic_orig;
    /* Allocate recon picture if analysis save/load is enabled */
    std::priority_queue<int64_t>* pts_queue = cliopt.output->needPTS() ? new std::priority_queue<int64_t>() : NULL;
    x265_picture *pic_recon = (cliopt.recon || param->analysisSave || param->analysisLoad || pts_queue || reconPlay || param->csvLogLevel) ? &pic_out : NULL;
    uint32_t inFrameCount = 0;
    uint32_t outFrameCount = 0;
    x265_nal *p_nal;
    x265_stats stats;
    uint32_t nal;
    int16_t *errorBuf = NULL;
    bool bDolbyVisionRPU = false;
    uint8_t *rpuPayload = NULL;
    int ret = 0;
    int inputPicNum = 1;
    x265_picture picField1, picField2;

    if (!param->bRepeatHeaders && !param->bEnableSvtHevc)
    {
        if (api->encoder_headers(encoder, &p_nal, &nal) < 0)
        {
            x265_log(param, X265_LOG_ERROR, "Failure generating stream headers\n");
            ret = 3;
            goto fail;
        }
        else
            cliopt.totalbytes += cliopt.output->writeHeaders(p_nal, nal);
    }

    if (param->bField && param->interlaceMode)
    {
        api->picture_init(param, &picField1);
        api->picture_init(param, &picField2);
        // return back the original height of input
        param->sourceHeight *= 2;
        api->picture_init(param, pic_in);
    }
    else
        api->picture_init(param, pic_in);

    if (param->dolbyProfile && cliopt.dolbyVisionRpu)
    {
        rpuPayload = X265_MALLOC(uint8_t, 1024);
        pic_in->rpu.payload = rpuPayload;
        if (pic_in->rpu.payload)
            bDolbyVisionRPU = true;
    }
    
    if (cliopt.bDither)
    {
        errorBuf = X265_MALLOC(int16_t, param->sourceWidth + 1);
        if (errorBuf)
            memset(errorBuf, 0, (param->sourceWidth + 1) * sizeof(int16_t));
        else
            cliopt.bDither = false;
    }

    // main encoder loop
    while (pic_in && !b_ctrl_c && !killed)
    {
        pic_orig.poc = (param->bField && param->interlaceMode) ? inFrameCount * 2 : inFrameCount;
        if (cliopt.qpfile)
        {
            if (!cliopt.parseQPFile(pic_orig))
            {
                x265_log(NULL, X265_LOG_ERROR, "can't parse qpfile for frame %d\n", pic_in->poc);
                fclose(cliopt.qpfile);
                cliopt.qpfile = NULL;
            }
        }

        if (cliopt.framesToBeEncoded && inFrameCount >= cliopt.framesToBeEncoded)
            pic_in = NULL;
        else if (cliopt.input->readPicture(pic_orig))
            inFrameCount++;
        else
            pic_in = NULL;

        if (pic_in)
        {
            if (pic_in->bitDepth > param->internalBitDepth && cliopt.bDither)
            {
                x265_dither_image(pic_in, cliopt.input->getWidth(), cliopt.input->getHeight(), errorBuf, param->internalBitDepth);
                pic_in->bitDepth = param->internalBitDepth;
            }
            /* Overwrite PTS */
            pic_in->pts = pic_in->poc;

            // convert to field
            if (param->bField && param->interlaceMode)
            {
                int height = pic_in->height >> 1;
                
                int static bCreated = 0;
                if (bCreated == 0)
                {
                    bCreated = 1;
                    inputPicNum = 2;
                    picField1.fieldNum = 1;
                    picField2.fieldNum = 2;

                    picField1.bitDepth = picField2.bitDepth = pic_in->bitDepth;
                    picField1.colorSpace = picField2.colorSpace = pic_in->colorSpace;
                    picField1.height = picField2.height = pic_in->height >> 1;
                    picField1.framesize = picField2.framesize = pic_in->framesize >> 1;

                    size_t fieldFrameSize = (size_t)pic_in->framesize >> 1;
                    char* field1Buf = X265_MALLOC(char, fieldFrameSize);
                    char* field2Buf = X265_MALLOC(char, fieldFrameSize);
  
                    int stride = picField1.stride[0] = picField2.stride[0] = pic_in->stride[0];
                    uint64_t framesize = stride * (height >> x265_cli_csps[pic_in->colorSpace].height[0]);
                    picField1.planes[0] = field1Buf;
                    picField2.planes[0] = field2Buf;
                    for (int i = 1; i < x265_cli_csps[pic_in->colorSpace].planes; i++)
                    {
                        picField1.planes[i] = field1Buf + framesize;
                        picField2.planes[i] = field2Buf + framesize;

                        stride = picField1.stride[i] = picField2.stride[i] = pic_in->stride[i];
                        framesize += (stride * (height >> x265_cli_csps[pic_in->colorSpace].height[i]));
                    }
                    assert(framesize  == picField1.framesize);
                }

                picField1.pts = picField1.poc = pic_in->poc;
                picField2.pts = picField2.poc = pic_in->poc + 1;

                picField1.userSEI = picField2.userSEI = pic_in->userSEI;

                //if (pic_in->userData)
                //{
                //    // Have to handle userData here
                //}

                if (pic_in->framesize)
                {
                    for (int i = 0; i < x265_cli_csps[pic_in->colorSpace].planes; i++)
                    {
                        char* srcP1 = (char*)pic_in->planes[i];
                        char* srcP2 = (char*)pic_in->planes[i] + pic_in->stride[i];
                        char* p1 = (char*)picField1.planes[i];
                        char* p2 = (char*)picField2.planes[i];

                        int stride = picField1.stride[i];

                        for (int y = 0; y < (height >> x265_cli_csps[pic_in->colorSpace].height[i]); y++)
                        {
                            memcpy(p1, srcP1, stride);
                            memcpy(p2, srcP2, stride);
                            srcP1 += 2*stride;
                            srcP2 += 2*stride;
                            p1 += stride;
                            p2 += stride;
                        }
                    }
                }
            }

            if (bDolbyVisionRPU)
            {
                if (param->bField && param->interlaceMode)
                {
                    if (rpuParser(&picField1, cliopt.dolbyVisionRpu) > 0)
                        goto fail;
                    if (rpuParser(&picField2, cliopt.dolbyVisionRpu) > 0)
                        goto fail;
                }
                else
                {
                    if (rpuParser(pic_in, cliopt.dolbyVisionRpu) > 0)
                        goto fail;
                }
            }
        }
                
        for (int inputNum = 0; inputNum < inputPicNum; inputNum++)
        {  
            x265_picture *picInput = NULL;
            if (inputPicNum == 2)
                picInput = pic_in ? (inputNum ? &picField2 : &picField1) : NULL;
            else
                picInput = pic_in;

            int numEncoded = api->encoder_encode( encoder, &p_nal, &nal, picInput, pic_recon );
            if( numEncoded < 0 )
            {
                b_ctrl_c = 1;
                ret = 4;
                break;
            }

            if (reconPlay && numEncoded)
                reconPlay->writePicture(*pic_recon);

            outFrameCount += numEncoded;

            if (numEncoded && pic_recon && cliopt.recon)
                cliopt.recon->writePicture(pic_out);
            if (nal)
            {
                cliopt.totalbytes += cliopt.output->writeFrame(p_nal, nal, pic_out);
                if (pts_queue)
                {
                    pts_queue->push(-pic_out.pts);
                    if (pts_queue->size() > 2)
                        pts_queue->pop();
                }
            }
            cliopt.printStatus( outFrameCount );
        }
    }

    /* Flush the encoder */
    while (!b_ctrl_c && !killed)
    {
        int numEncoded = api->encoder_encode(encoder, &p_nal, &nal, NULL, pic_recon);
        if (numEncoded < 0)
        {
            ret = 4;
            break;
        }

        if (reconPlay && numEncoded)
            reconPlay->writePicture(*pic_recon);

        outFrameCount += numEncoded;
        if (numEncoded && pic_recon && cliopt.recon)
            cliopt.recon->writePicture(pic_out);
        if (nal)
        {
            cliopt.totalbytes += cliopt.output->writeFrame(p_nal, nal, pic_out);
            if (pts_queue)
            {
                pts_queue->push(-pic_out.pts);
                if (pts_queue->size() > 2)
                    pts_queue->pop();
            }
        }

        cliopt.printStatus(outFrameCount);

        if (!numEncoded)
            break;
    }
  
    if (bDolbyVisionRPU)
    {
        if(fgetc(cliopt.dolbyVisionRpu) != EOF)
            x265_log(NULL, X265_LOG_WARNING, "Dolby Vision RPU count is greater than frame count\n");
        x265_log(NULL, X265_LOG_INFO, "VES muxing with Dolby Vision RPU file successful\n");
    }

    /* clear progress report */
    if (cliopt.bProgress)
        fprintf(stderr, "%*s\r", 80, " ");

fail:

    delete reconPlay;

    api->encoder_get_stats(encoder, &stats, sizeof(stats));
    if (param->csvfn && !b_ctrl_c && !killed)
#if ENABLE_LIBVMAF
        api->vmaf_encoder_log(encoder, argc, argv, param, vmafdata);
#else
        api->encoder_log(encoder, argc, argv);
#endif
    api->encoder_close(encoder);

    int64_t second_largest_pts = 0;
    int64_t largest_pts = 0;
    if (pts_queue && pts_queue->size() >= 2)
    {
        second_largest_pts = -pts_queue->top();
        pts_queue->pop();
        largest_pts = -pts_queue->top();
        pts_queue->pop();
        delete pts_queue;
        pts_queue = NULL;
    }
    cliopt.output->closeFile(largest_pts, second_largest_pts);

    if (b_ctrl_c || killed)
        general_log(param, NULL, X265_LOG_INFO, "aborted at input frame %d, output frame %d\n",
                    cliopt.seek + inFrameCount, stats.encodedPictureCount);

    api->cleanup(); /* Free library singletons */

    cliopt.destroy();

    api->param_free(param);

    X265_FREE(errorBuf);
    X265_FREE(rpuPayload);

    SetConsoleTitle(orgConsoleTitle);
    SetThreadExecutionState(ES_CONTINUOUS);

#if _WIN32
    if (argv != orgArgv)
    {
        free(argv);
        argv = orgArgv;
    }
#endif

#if HAVE_VLD
    assert(VLDReportLeaks() == 0);
#endif

    return ret;
}
#ifndef USE_X265_LIB
int main(int argc, char **argv)
{
    std::atomic<bool> killed;
    return x265main(argc, argv, nullptr, nullptr, killed);
}
#endif
