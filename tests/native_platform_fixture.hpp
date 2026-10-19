#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <cstdlib>

namespace native_fixture {
inline QString modulePath(HMODULE module=nullptr) {
    wchar_t path[32768]{}; const DWORD n=GetModuleFileNameW(module,path,32768);
    return n && n<32768 ? QString::fromWCharArray(path,int(n)) : QString();
}
inline QString environment(const wchar_t* key) {
    const DWORD n=GetEnvironmentVariableW(key,nullptr,0); if(!n) return {};
    std::wstring value(n,L'\0');const DWORD used=GetEnvironmentVariableW(key,value.data(),n);
    return used<n ? QString::fromWCharArray(value.data(),int(used)) : QString();
}
inline bool setEnvironment(const wchar_t* key,const QString& value) {
    return _wputenv_s(key,reinterpret_cast<const wchar_t*>(value.utf16()))==0 &&
        SetEnvironmentVariableW(key,reinterpret_cast<const wchar_t*>(value.utf16()));
}
inline QString hash(const QString& path) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&file)?QString::fromLatin1(h.result().toHex()):QString();
}
inline QString canonical(const QString& path) {return QFileInfo(path).canonicalFilePath();}
inline bool samePath(const QString& a,const QString& b) {
    return !canonical(a).isEmpty() && canonical(a).compare(canonical(b),Qt::CaseInsensitive)==0;
}
inline QString qtDirectory() {return QFileInfo(modulePath(GetModuleHandleW(L"Qt6Core.dll"))).absolutePath();}
struct ShellCapture {int calls{};QString folder;QStringList children;UINT count{};DWORD flags{};HRESULT result=S_OK;};
ShellCapture& shellCapture();
HRESULT WINAPI captureSelection(PCIDLIST_ABSOLUTE folder,UINT count,PCUITEMID_CHILD_ARRAY children,DWORD flags);

class Fixture {
    int serial{};
    QString originalCapture=environment(L"GLANCELOT_TEST_LAUNCH_CAPTURE");
    bool hadCapture=GetEnvironmentVariableW(L"GLANCELOT_TEST_LAUNCH_CAPTURE",nullptr,0)!=0;
public:
    QString sandbox=environment(L"GLANCELOT_NATIVE_SANDBOX");
    QString capture;
    QString error;
    ~Fixture() {
        if(hadCapture) setEnvironment(L"GLANCELOT_TEST_LAUNCH_CAPTURE",originalCapture);
        else {_wputenv_s(L"GLANCELOT_TEST_LAUNCH_CAPTURE",L"");SetEnvironmentVariableW(L"GLANCELOT_TEST_LAUNCH_CAPTURE",nullptr);}
    }
    bool prepare() {
        if(!samePath(QCoreApplication::applicationDirPath(),sandbox) ||
           hash(QCoreApplication::applicationFilePath())!=environment(L"GLANCELOT_NATIVE_RUNNER_SHA") ||
           !samePath(qtDirectory(),environment(L"GLANCELOT_NATIVE_QT_DIR"))) {error="private runner/Qt identity mismatch";return false;}
        for(const auto* name:{"explorer.exe","wt.exe","powershell.exe"})
            if(hash(QDir(sandbox).filePath(name))!=environment(L"GLANCELOT_NATIVE_STUB_SHA")) {error="private helper identity mismatch";return false;}
        if(QDir::currentPath()!=sandbox || environment(L"PATH")!=sandbox+";"+qtDirectory()) {error="private CWD/PATH mismatch";return false;}
        return true;
    }
    bool begin() {
        capture=QDir(sandbox).filePath(QString("captures/request-%1.json").arg(++serial));
        if(QFileInfo::exists(capture) || !setEnvironment(L"GLANCELOT_TEST_LAUNCH_CAPTURE",capture)) {error="capture isolation failed";return false;}
        return true;
    }
    bool noCapture() const {return !QFileInfo::exists(capture);}
    bool verify(const QString& name,const QStringList& args,const QString& cwd) {
        QElapsedTimer timer;timer.start();while(!QFileInfo::exists(capture) && timer.elapsed()<1500) QThread::msleep(5);
        QFile file(capture); if(!file.open(QIODevice::ReadOnly) || file.size()>256*1024) {error="missing/bounded capture";return false;}
        const auto record=QJsonDocument::fromJson(file.readAll()).object();file.close();
        const auto module=QDir(sandbox).filePath(name);
        if(!samePath(record.value("module").toString(),module) || record.value("module_sha256").toString()!=environment(L"GLANCELOT_NATIVE_STUB_SHA") ||
           record.value("token").toString()!=environment(L"GLANCELOT_NATIVE_TOKEN") ||
           record.value("arguments").toArray()!=QJsonArray::fromStringList(args) || !samePath(record.value("cwd").toString(),cwd) ||
           record.value("console").toBool() || record.value("window_count").toInt(-1)!=0) {error="captured module/argv/CWD/canary mismatch";return false;}
        const DWORD pid=DWORD(record.value("pid").toInteger()); if(!pid){error="missing helper pid";return false;}
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(process) {
            wchar_t path[32768]{};DWORD length=32768;
            const bool own=QueryFullProcessImageNameW(process,0,path,&length) && samePath(QString::fromWCharArray(path,int(length)),module);
            const DWORD wait=own?WaitForSingleObject(process,1500):WAIT_FAILED;CloseHandle(process);
            if(!own || wait!=WAIT_OBJECT_0) {error="owned helper did not exit or pid identity changed";return false;}
        } else if(GetLastError()!=ERROR_INVALID_PARAMETER) {error="cannot verify helper termination";return false;}
        return QFile::remove(capture);
    }
};

#ifdef GLANCELOT_TEST_LAUNCH_STUB
inline int bootstrap(const QStringList& originalArguments) {
    QTemporaryDir temp(QDir::tempPath()+QString::fromUtf8("/glancelot native \U0001f9ea space-XXXXXX"));
    if(!temp.isValid()) return 101;
    const QString sandbox=canonical(temp.path()), runner=QDir(sandbox).filePath("platform_integration_tests.exe");
    const QString stub=QString::fromUtf8(GLANCELOT_TEST_LAUNCH_STUB);
    if(hash(stub).isEmpty() || !QFile::copy(QCoreApplication::applicationFilePath(),runner) || !QDir(sandbox).mkdir("captures")) return 102;
    for(const auto* name:{"explorer.exe","wt.exe","powershell.exe"}) if(!QFile::copy(stub,QDir(sandbox).filePath(name))) return 103;
    QProcessEnvironment env=QProcessEnvironment::systemEnvironment();
    env.insert("PATH",sandbox+";"+qtDirectory());env.insert("GLANCELOT_NATIVE_SANDBOX",sandbox);
    env.insert("GLANCELOT_NATIVE_QT_DIR",qtDirectory());env.insert("GLANCELOT_NATIVE_RUNNER_SHA",hash(runner));
    env.insert("GLANCELOT_NATIVE_STUB_SHA",hash(stub));env.insert("GLANCELOT_NATIVE_TOKEN",QUuid::createUuid().toString());
    // Resolve relative QtTest log destinations before changing the child's working directory.
    QStringList args=originalArguments;
    for(qsizetype i=1;i<args.size();++i) if(args[i-1]=="-o" && args[i]!="-" && !args[i].startsWith("-,")) {
        const qsizetype comma=args[i].lastIndexOf(','); const QString file=comma>=0?args[i].left(comma):args[i];
        if(QDir::isRelativePath(file)) args[i]=QFileInfo(file).absoluteFilePath()+(comma>=0?args[i].mid(comma):QString());
    }
    HANDLE job=CreateJobObjectW(nullptr,nullptr); if(!job) return 104;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit))) {CloseHandle(job);return 105;}
    QProcess child;child.setProcessEnvironment(env);child.setWorkingDirectory(sandbox);child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arg){arg->flags|=CREATE_SUSPENDED|CREATE_NO_WINDOW;});
    child.start(runner,args.mid(1));
    if(!child.waitForStarted(2000)) {CloseHandle(job);return 106;}
    HANDLE process=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(child.processId()));
    bool assigned=process && AssignProcessToJobObject(job,process);if(process) CloseHandle(process);
    bool resumed=false;
    if(assigned) {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
        if(snapshot!=INVALID_HANDLE_VALUE) {
            if(Thread32First(snapshot,&entry)) do {if(entry.th32OwnerProcessID==DWORD(child.processId())) {
                HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);
                if(thread){resumed=ResumeThread(thread)!=DWORD(-1);CloseHandle(thread);}break;
            }} while(Thread32Next(snapshot,&entry));CloseHandle(snapshot);
        }
    }
    if(!assigned || !resumed) {child.kill();child.waitForFinished(1000);CloseHandle(job);return 107;}
    const bool finished=child.waitForFinished(22000);
    if(!finished) {TerminateJobObject(job,108);child.waitForFinished(1000);}
    int result=finished && child.exitStatus()==QProcess::NormalExit?child.exitCode():108;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr) || accounting.ActiveProcesses!=0) {
        if(result==0) result=109; // An owned helper outlived the test child: never silently accept a leak.
        TerminateJobObject(job,109);
    }
    // Closing this handle also kills any owned helper that outlived its request, including abnormal child exits.
    CloseHandle(job);return result;
}
#endif
} // namespace native_fixture
#endif
