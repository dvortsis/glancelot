#pragma once
#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QLibrary>
#include <atomic>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace glancelot::output_color {
inline constexpr int cubeSize=33;
inline constexpr qint64 atlasBytes=qint64(cubeSize)*cubeSize*cubeSize*4;
inline constexpr qint64 maximumProfileBytes=4*1024*1024;
// One process-wide permit: closing/reopening windows cannot accumulate background tasks.
inline std::atomic_bool workerBusy{false};
struct Result { QImage atlas; QString status; quint64 revision{}; };

inline QImage makeAtlas(const QColorSpace& destination) {
    if (!destination.isValid() || destination==QColorSpace(QColorSpace::SRgb)) return {};
    QImage input(cubeSize*cubeSize,cubeSize,QImage::Format_RGBA8888);
    input.setColorSpace(QColorSpace::SRgb);
    for (int g=0;g<cubeSize;++g) for (int b=0;b<cubeSize;++b) for (int r=0;r<cubeSize;++r)
        input.setPixelColor(b*cubeSize+r,g,QColor(qRound(r*255.0/32),qRound(g*255.0/32),qRound(b*255.0/32)));
    return input.convertedToColorSpace(destination,QImage::Format_RGBA8888);
}

// Worker only. Public QScreen does not expose ICC in Qt6.8. Other platforms use the
// explicit sRGB fallback until a native profile is available; never claim an unknown profile.
inline QColorSpace systemProfile(const QString& screenName) {
#ifdef Q_OS_WIN
    wchar_t directory[MAX_PATH]{};
    const UINT n=GetSystemDirectoryW(directory,MAX_PATH);
    if (!n || n>=MAX_PATH) return {};
    QLibrary gdi(QString::fromWCharArray(directory)+QStringLiteral("/gdi32.dll"));
    const auto createDc=reinterpret_cast<decltype(&CreateDCW)>(gdi.resolve("CreateDCW"));
    const auto getProfile=reinterpret_cast<decltype(&GetICMProfileW)>(gdi.resolve("GetICMProfileW"));
    const auto deleteDc=reinterpret_cast<decltype(&DeleteDC)>(gdi.resolve("DeleteDC"));
    if (!createDc || !getProfile || !deleteDc) return {};
    const auto name=screenName.toStdWString();
    HDC dc=createDc(L"DISPLAY",name.c_str(),nullptr,nullptr);
    if (!dc) return {};
    DWORD length=0; getProfile(dc,&length,nullptr);
    if (!length || length>32768) { deleteDc(dc); return {}; }
    std::wstring path(length,L'\0');
    const bool found=getProfile(dc,&length,path.data()); deleteDc(dc);
    if (!found) return {};
    path.resize(wcslen(path.c_str()));
    // Do not let an unreachable UNC or mapped network profile hold the only worker.
    if (path.size()<3 || path[1]!=L':' || GetDriveTypeW(path.substr(0,3).c_str())!=DRIVE_FIXED) return {};
    QFile file(QString::fromStdWString(path));
    if (!file.open(QIODevice::ReadOnly) || file.size()<=0 || file.size()>maximumProfileBytes) return {};
    const auto bytes=file.read(maximumProfileBytes);
    if (!file.atEnd()) return {};
    return QColorSpace::fromIccProfile(bytes);
#else
    Q_UNUSED(screenName);
    return {};
#endif
}
inline Result build(const QString& screenName, QColorSpace destination, bool injected, quint64 revision) {
    struct Release { ~Release() { workerBusy.store(false); } } release;
    if (!injected) destination=systemProfile(screenName);
    if (!destination.isValid()) return {{},QStringLiteral("sRGB fallback (monitor profile unavailable)"),revision};
    auto atlas=makeAtlas(destination);
    if (atlas.isNull() && destination!=QColorSpace(QColorSpace::SRgb))
        return {{},QStringLiteral("sRGB fallback (monitor transform unavailable)"),revision};
    return {atlas,QStringLiteral("SDR output profile: ")+destination.description(),revision};
}
} // namespace glancelot::output_color
