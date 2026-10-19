#include "glancelot/platform_integration.hpp"
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

namespace glancelot {

bool PlatformIntegration::revealInFileManager(const QString& path) {
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    if (!fi.exists()) return false;

#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
    if (fi.isDir()) {
        return QProcess::startDetached("explorer.exe", {native});
    }
    // Try SHOpenFolderAndSelectItems via shell API
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInitialized = (hrCo == S_OK || hrCo == S_FALSE);
    PIDLIST_ABSOLUTE pidlFile = ILCreateFromPathW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (pidlFile) {
        PIDLIST_ABSOLUTE pidlFolder = ILClone(pidlFile);
        ILRemoveLastID(pidlFolder);
        PCUITEMID_CHILD pidlChild = ILFindLastID(pidlFile);
        HRESULT hr = SHOpenFolderAndSelectItems(pidlFolder, 1, &pidlChild, 0);
        ILFree(pidlFolder);
        ILFree(pidlFile);
        if (comInitialized) CoUninitialize();
        if (SUCCEEDED(hr)) return true;
    } else if (comInitialized) {
        CoUninitialize();
    }
    // Fallback to explorer.exe /select,"path"
    return QProcess::startDetached("explorer.exe", {"/select,", native});
#else
    // On Linux/X11, if it's a file, open its parent folder
    const QString targetDir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    return QProcess::startDetached("xdg-open", {targetDir});
#endif
}

bool PlatformIntegration::openTerminal(const QString& directoryPath) {
    if (directoryPath.isEmpty()) return false;
    const QFileInfo fi(directoryPath);
    if (!fi.exists()) return false;
    const QString targetDir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (!QDir(targetDir).exists()) return false;

#ifdef Q_OS_WIN
    const QString nativeDir = QDir::toNativeSeparators(targetDir);
    const QString wtPath = QStandardPaths::findExecutable("wt.exe");
    if (!wtPath.isEmpty()) {
        if (QProcess::startDetached(wtPath, {"-d", nativeDir})) return true;
    }
    // Fallback to PowerShell without shell string injection
    QString escaped = nativeDir;
    escaped.replace("'", "''");
    return QProcess::startDetached("powershell.exe", {"-NoExit", "-Command", QString("Set-Location -LiteralPath '%1'").arg(escaped)});
#else
    const QString termEnv = qEnvironmentVariable("TERMINAL");
    if (!termEnv.isEmpty()) {
        QProcess p;
        p.setWorkingDirectory(targetDir);
        if (QProcess::startDetached(termEnv, {}, targetDir)) return true;
    }
    for (const QString& candidate : {"x-terminal-emulator", "gnome-terminal", "konsole", "xfce4-terminal", "xterm"}) {
        const QString exec = QStandardPaths::findExecutable(candidate);
        if (!exec.isEmpty()) {
            if (QProcess::startDetached(exec, {}, targetDir)) return true;
        }
    }
    return false;
#endif
}

} // namespace glancelot
