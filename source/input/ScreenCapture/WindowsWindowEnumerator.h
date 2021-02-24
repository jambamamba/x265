#pragma once

#if 0
#include "WindowEnumeratorInterface.h"

#include <QMap>
#include <windows.h>

class WindowsWindowEnumerator : public WindowEnumeratorInterface
{
    Q_OBJECT
public:
    WindowsWindowEnumerator(QObject *parent = nullptr);
protected:
    static BOOL EnumWindowsProc(HWND   hwnd, LPARAM lParam);
    void enumTopLevelWindows() override;
    virtual QRect windowRectWithPoint(int x, int y) override;

    QMap<qint64/*hwnd*/, QRect> m_map;
};
#endif//0
