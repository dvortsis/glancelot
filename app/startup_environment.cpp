#include "startup_environment.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdlib>

namespace glancelot::startup {
OriginalConfig captureOriginalConfig() {
    const auto value=qEnvironmentVariable("GLANCELOT_CONFIG_DIR");
    return value.isEmpty()?OriginalConfig{}:OriginalConfig{false,QDir::cleanPath(QFileInfo(value).absoluteFilePath())};
}
QString resolveOriginalConfig(const OriginalConfig& original) {
    const auto value=original.useDefault?QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation):original.overrideDirectory;
    return QDir::cleanPath(QFileInfo(value).absoluteFilePath());
}
bool setConfigOverride(const QString& directory) {
    if(directory.contains(QChar(0)))return false;
#ifdef Q_OS_WIN
    // Qt's wide environment reader uses the CRT wide environment. A narrow
    // qputenv(toUtf8()) can corrupt names outside the Windows ANSI code page.
    const auto wide=directory.toStdWString();
    if(_wputenv_s(L"GLANCELOT_CONFIG_DIR",wide.c_str())!=0)return false;
#else
    if(!qputenv("GLANCELOT_CONFIG_DIR",QFile::encodeName(directory)))return false;
#endif
    return qEnvironmentVariable("GLANCELOT_CONFIG_DIR")==directory;
}
SafeEnvironment prepareSafeEnvironment(const QString& temporaryParent) {
    using recovery::Code;SafeEnvironment result;result.status.code=Code::WriteFailed;
    if(QCoreApplication::instance()) {
        result.status={Code::Conflict,"Safe mode must be prepared before application construction.",0};return result;
    }
    const auto parent=temporaryParent.isEmpty()?QDir::tempPath():temporaryParent;
    if(!QDir::isAbsolutePath(parent)||!QFileInfo(parent).isDir()) {
        result.status.detail="Safe-mode temporary parent is unavailable.";return result;
    }
    auto owner=std::make_shared<QTemporaryDir>(QDir(parent).filePath("Glancelot-safe-XXXXXX"));
    if(!owner->isValid()){result.status.detail="Could not create private safe-mode settings.";return result;}
    if(!setConfigOverride(owner->path())){result.status.detail="Could not redirect safe-mode settings.";return result;}
    // Keep the RHI scene graph and QQuickRhiItem; remove the incompatible Qt
    // Quick software adaptation and inherited device/debug selections locally.
    for(const auto* variable:{"QT_QUICK_BACKEND","QMLSCENE_DEVICE","QT_D3D_ADAPTER_INDEX","QT_D3D_FEATURE_LEVEL",
                             "QT_VK_PHYSICAL_DEVICE_INDEX","QSG_RHI_DEBUG_LAYER","QSG_RHI_DEBUG_MARKERS"})
        if(!qunsetenv(variable))return result;
    if(!qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER","1"))return result;
#ifdef Q_OS_WIN
    if(!qputenv("QSG_RHI_BACKEND","d3d11"))return result;
    result.requestedBackend="d3d11-warp";
#elif defined(Q_OS_LINUX)
    if(!qunsetenv("MESA_LOADER_DRIVER_OVERRIDE")||!qunsetenv("DRI_PRIME")||!qunsetenv("__NV_PRIME_RENDER_OFFLOAD")||
       !qputenv("QSG_RHI_BACKEND","opengl")||!qputenv("LIBGL_ALWAYS_SOFTWARE","1")||!qputenv("GALLIUM_DRIVER","llvmpipe"))return result;
    result.requestedBackend="opengl-software";
#else
    result.status={Code::Unsupported,"Safe-mode software RHI is not qualified on this platform.",0};return result;
#endif
    if(!qputenv("QT_OPENGL","software"))return result;
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    result.context={owner->path(),std::move(owner)};result.status.code=Code::Ok;return result;
}
}
