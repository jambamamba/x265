#pragma once

#include <string.h>
#include <iostream>
#include <memory>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "output.h"

constexpr char UDP[] = "udp://";

class UdpWriter : public x265::OutputFile
{
public:
    UdpWriter();
    ~UdpWriter();
    static UdpWriter *construct(const char* fname);
    bool initialize();
    void setendpoint(const char* destinationIp, unsigned short port);
    ssize_t write(const char *data, ssize_t bytes) const;

    bool isFail() const { return false; }
    bool needPTS() const { return false; }
    void release() { delete this; }
    const char* getName() const { return "udp"; }

    void setParam(x265_param* param) {}
    int writeHeaders(const x265_nal* nal, uint32_t nalcount);
    int writeFrame(const x265_nal* nal, uint32_t nalcount, x265_picture&);
    void closeFile(int64_t largest_pts, int64_t second_largest_pts);

protected:
    int sock;
    struct sockaddr_in sa;
    FILE *fp;
};
