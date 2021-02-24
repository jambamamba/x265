#include "ScreenCapture.h"

#include <algorithm>
#include <QImage>
#include <QScreen>
#include <QList>
#include <QGuiApplication>
#include <QCursor>
#include <QPixmap>

ScreenCapture::ScreenCapture()
{
    InitAvailableScreens();
}
//--input /dev/screen --input-res 1920x1080 --fps 1 --preset ultrafast --tune psnr --tune ssim --tune fastdecode  --tune zerolatency --output udp://127.0.0.1:7878
bool ScreenCapture::CaptureScreen(char **rgb, unsigned long *rgbSz)
{
//    *rgbSz = 1260000;
//    *rgb = (char*)realloc(*rgb, *rgbSz);

//    FILE* fp = fopen("/home/dev/oosman/repos/x265/rgb.data", "rb");
//    fread(*rgb, *rgbSz, 1, fp);
//    fclose(fp);
//    return true;

    QScreen* screen = GetCurrentScreen();
    QImage img = screen->grabWindow(0).toImage();
    img = img.scaled(600, 700);

    *rgbSz = img.width() * img.height() * 3;
    *rgb = (char*)realloc(*rgb, *rgbSz);

    for(int j = 0; j < img.height(); ++j)
    {
        for(int i = 0; i < img.width(); ++i)
        {
            uint8_t r = img.bits()[j * img.width()*4 + i*4 + 2];
            uint8_t g = img.bits()[j * img.width()*4 + i*4 + 1];
            uint8_t b = img.bits()[j * img.width()*4 + i*4 + 0];
            (*rgb)[j * img.width()*3 + i*3] = (int8_t)r;
            (*rgb)[j * img.width()*3 + i*3 + 1] = (int8_t)g;
            (*rgb)[j * img.width()*3 + i*3 + 2] = (int8_t)b;
        }
    }

//    QImage img2(img.width(), img.height(), QImage::Format_RGB888);
//    for(int j = 0; j < img.height(); ++j)
//    {
//        for(int i = 0; i < img.width(); ++i)
//        {
//            int8_t *bytes = (int8_t *)(*rgb);
//            int32_t r = (bytes[j * img.width()*3 + i*3]) & 0xff;
//            int32_t g = (bytes[j * img.width()*3 + i*3 + 1]) & 0xff;
//            int32_t b = (bytes[j * img.width()*3 + i*3 + 2]) & 0xff;
//            img2.bits()[j * img.width()*3 + i*3] = r;
//            img2.bits()[j * img.width()*3 + i*3 + 1] = g;
//            img2.bits()[j * img.width()*3 + i*3 + 2] = b;
//        }
//    }
//    img2.save("/home/dev/oosman/foo/img.png");

    return true;
}
void ScreenCapture::InitAvailableScreens()
{
    int argc = 0;
    char ** argv = nullptr;
    static QGuiApplication app(argc, argv, 0);
    m_screens = QGuiApplication::screens();
    std::sort(m_screens.begin(), m_screens.end(),
              [](QScreen *a, QScreen *b){
        return (a->geometry().x() != b->geometry().x()) ?
                    a->geometry().x() < b->geometry().x() :
                    a->geometry().y() < b->geometry().y();
    });
}
QScreen *ScreenCapture::GetCurrentScreen() const
{
    int screen_idx = 0;
    for(auto screen : m_screens)
    {
        if(screen->geometry().contains(QCursor::pos()))
        {
            return m_screens[screen_idx];
        }
        screen_idx++;
    }
    return m_screens[0];
}

