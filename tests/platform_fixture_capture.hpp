#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>
#include <deque>
#include <optional>
namespace platform_fixture {
struct Launch {QString program;QStringList arguments;QString workingDirectory;};
struct ShellCapture {int calls{};QString folder;QStringList children;UINT count{};DWORD flags{};HRESULT result=S_OK;};
struct State {
    QString captureRoot,terminalExecutable,error;
    QList<Launch> launches;
    QList<QJsonObject> nativeRecords;
    std::deque<bool> launchResults;
    int lookupCalls{};
    ShellCapture shell;
};
State& state();
inline QString helperProgram(){return QString::fromUtf8(GLANCELOT_TEST_ARGV_CAPTURE);}
inline bool samePath(const QString& a,const QString& b) {
    const auto x=QFileInfo(a).canonicalFilePath(),y=QFileInfo(b).canonicalFilePath();
    return !x.isEmpty()&&!y.isEmpty()&&x.compare(y,Qt::CaseInsensitive)==0;
}
std::optional<QJsonObject> captureArguments(const QStringList& arguments,const QString& directory);
struct Process {static bool startDetached(const QString& program,const QStringList& arguments,const QString& directory=QString());};
struct Paths {static QString findExecutable(const QString& name);};
HRESULT WINAPI captureSelection(PCIDLIST_ABSOLUTE folder,UINT count,PCUITEMID_CHILD_ARRAY children,DWORD flags);
}
#endif
