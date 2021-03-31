#pragma once


#include "input/rgbreader.h"

class QScreen;

class ScreenCapture : public rgbreader
{
public:
    ScreenCapture();
    virtual int readImage(char **rgb, ssize_t *bytes, int width, int height) override;
protected:
    void InitAvailableScreens();
    QScreen *GetCurrentScreen() const;
};

