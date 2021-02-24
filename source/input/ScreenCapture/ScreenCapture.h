#pragma once

#include <QList>

class QScreen;

class ScreenCapture
{
public:
    ScreenCapture();
    bool CaptureScreen(char **rgb, unsigned long *rgbSz);
protected:
    void InitAvailableScreens();
    QScreen *GetCurrentScreen() const;
    QList<QScreen *> m_screens;
};

