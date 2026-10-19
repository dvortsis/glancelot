// GUI subsystem helper: no application object, event loop, console, window or command execution.
#include "native_platform_fixture.hpp"
#include <QSaveFile>
namespace {
BOOL CALLBACK countWindow(HWND window,LPARAM opaque) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()) ++*reinterpret_cast<int*>(opaque);return TRUE;
}
}
int main(int,char**) {
    using namespace native_fixture;
    int count=0;LPWSTR* wide=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!wide || count<1 || count>64) {if(wide) LocalFree(wide);return 91;}
    QStringList args;for(int i=1;i<count;++i) args.append(QString::fromWCharArray(wide[i]));LocalFree(wide);
    wchar_t cwd[32768]{};const DWORD used=GetCurrentDirectoryW(32768,cwd);if(!used || used>=32768)return 92;
    const QString sandbox=environment(L"GLANCELOT_NATIVE_SANDBOX"),destination=environment(L"GLANCELOT_TEST_LAUNCH_CAPTURE");
    const QString module=modulePath();
    if(!samePath(QFileInfo(module).absolutePath(),sandbox) || !samePath(QFileInfo(destination).absolutePath(),QDir(sandbox).filePath("captures")) ||
       QFileInfo::exists(destination) || environment(L"GLANCELOT_NATIVE_TOKEN").isEmpty()) return 93;
    int windows=0;EnumWindows(countWindow,reinterpret_cast<LPARAM>(&windows));
    const auto bytes=QJsonDocument(QJsonObject{{"arguments",QJsonArray::fromStringList(args)},{"cwd",QString::fromWCharArray(cwd,int(used))},
        {"module",module},{"module_sha256",hash(module)},{"pid",qint64(GetCurrentProcessId())},{"console",GetConsoleWindow()!=nullptr},
        {"window_count",windows},{"token",environment(L"GLANCELOT_NATIVE_TOKEN")}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>256*1024)return 94;
    QSaveFile output(destination);return output.open(QIODevice::WriteOnly) && output.write(bytes)==bytes.size() && output.commit()?0:95;
}
