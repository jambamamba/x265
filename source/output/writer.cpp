#include "writer.h"

BufferWriter::BufferWriter(std::function<int(const char *data, ssize_t bytes)> writeData)
    : _writeData(writeData)
{}

BufferWriter::~BufferWriter()
{}

BufferWriter *BufferWriter::construct(std::function<int(const char *data, ssize_t bytes)> writeData)
{
    return new BufferWriter(writeData);
}

int BufferWriter::writeHeaders(const x265_nal* nal, uint32_t nalcount)
{
    x265_picture unused;
    return writeFrame(nal, nalcount, unused);
}
int BufferWriter::writeFrame(const x265_nal* nal, uint32_t nalcount, x265_picture&)
{
    uint32_t bytes = 0;

    for (uint32_t i = 0; i < nalcount; i++)
    {
        int res = _writeData((const char*)nal->payload, nal->sizeBytes);
        if(res > 0)
        {
            bytes += res;
        }
        else if((uint32_t)res != nal->sizeBytes)
        {
            //osm fix me!
        }
        nal++;
    }

    return bytes;
}
void BufferWriter::closeFile(int64_t largest_pts, int64_t second_largest_pts)
{}

UdpWriter::UdpWriter()
    : sock(-1)
{
    fp = fopen("/tmp/frames.hevc", "wb");
}
UdpWriter::~UdpWriter()
{
    if(sock != -1)
    {close(sock); /* close the socket */}
    fclose(fp);
}
bool UdpWriter::initialize()
{
    /* create an Internet, datagram, socket using UDP */
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1)
    {
        /* if socket failed to initialize, exit */
        printf("Error Creating Socket");
        return false;
    }

    /* Zero out socket address */
    memset(&sa, 0, sizeof sa);

    /* The address is IPv4 */
    sa.sin_family = AF_INET;
    return true;
}
void UdpWriter::setendpoint(const char* destinationIp, unsigned short port)
{
    /* IPv4 adresses is a uint32_t, convert a string representation of the octets to the appropriate value */
    sa.sin_addr.s_addr = inet_addr(destinationIp);

    /* sockets are unsigned shorts, htons(x) ensures x is in network byte order, set the port to 7654 */
    sa.sin_port = htons(port);

}
ssize_t UdpWriter::write(const char *data, ssize_t bytes) const
{
    if(sock==-1)
    {
        return -1;
    }
    constexpr ssize_t DATAGRAM_SIZE = 8192;
    ssize_t total_sent  = 0;
    while(total_sent < bytes)
    {
        ssize_t bytes_sent = sendto(sock, &data[total_sent], std::min<ssize_t>(DATAGRAM_SIZE, bytes - total_sent), 0,(struct sockaddr*)&sa, sizeof sa);
        if (bytes_sent < 0)
        {
            printf("Error sending packet: %s\n", strerror(errno));
            return -1;
        }
        total_sent+=bytes_sent;
//        usleep(1000*100);
    }
    fwrite(data, bytes, 1, fp);
    fprintf(stderr, "total_sent: %u  \r", total_sent);
    return total_sent;
}

int UdpWriter::writeHeaders(const x265_nal *nal, uint32_t nalcount)
{
    uint32_t bytes = 0;

    for (uint32_t i = 0; i < nalcount; i++)
    {
        write((const char*)nal->payload, nal->sizeBytes);
        bytes += nal->sizeBytes;
        nal++;
    }

    return bytes;
}

int UdpWriter::writeFrame(const x265_nal *nal, uint32_t nalcount, x265_picture &)
{
    int bytes = 0;

    for (uint32_t i = 0; i < nalcount; i++)
    {
        write((const char*)nal->payload, nal->sizeBytes);
        bytes += nal->sizeBytes;
        nal++;
    }

    return bytes;
}

void UdpWriter::closeFile(int64_t largest_pts, int64_t second_largest_pts)
{
//    if (ofs != stdout)
//        fclose(ofs);
}

UdpWriter *UdpWriter::construct(const char* fname)
{
    std::string str = &fname[strlen(UDP)];
    std::string ip;
    std::string port;

    char delim = ':';
    std::size_t current, previous = 0;
    current = str.find(delim);
    while (current != std::string::npos)
    {
        ip = str.substr(previous, current - previous);
        previous = current + 1;
        current = str.find(delim, previous);
    }
    port = str.substr(previous, str.size() - previous);
    auto out = new UdpWriter();
    if(!out->initialize())
    {
        delete out;
        return nullptr;
    }
    out->setendpoint(ip.c_str(), std::stoi(port.c_str()));
    return out;
}
