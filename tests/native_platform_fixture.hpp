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
#include <string>

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
    if(a.isEmpty() || b.isEmpty()) return false;
    HANDLE first=CreateFileW(reinterpret_cast<LPCWSTR>(a.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    HANDLE second=CreateFileW(reinterpret_cast<LPCWSTR>(b.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    BY_HANDLE_FILE_INFORMATION x{},y{};
    const bool same=first!=INVALID_HANDLE_VALUE && second!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(first,&x) && GetFileInformationByHandle(second,&y) && x.dwVolumeSerialNumber==y.dwVolumeSerialNumber && x.nFileIndexHigh==y.nFileIndexHigh && x.nFileIndexLow==y.nFileIndexLow;
    if(first!=INVALID_HANDLE_VALUE) CloseHandle(first);
    if(second!=INVALID_HANDLE_VALUE) CloseHandle(second);
    return same;
}
inline QString qtDirectory() {
    HMODULE core=GetModuleHandleW(L"Qt6Core.dll");
    if(!core) core=GetModuleHandleW(L"Qt6Cored.dll");
    return core?QFileInfo(modulePath(core)).absolutePath():QString();
}
inline QString shortDirectory(const QString& path) {
    wchar_t buffer[32768]{};
    const DWORD n=GetShortPathNameW(reinterpret_cast<LPCWSTR>(path.utf16()),buffer,32768);
    if(!n || n>=32768) return {};
    const QString result=QString::fromWCharArray(buffer,int(n));
    for(const QChar c:result) if(c.unicode()>127) return {};
    HANDLE original=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    HANDLE alias=CreateFileW(buffer,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    BY_HANDLE_FILE_INFORMATION a{},b{};
    const bool same=original!=INVALID_HANDLE_VALUE && alias!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(original,&a) && GetFileInformationByHandle(alias,&b) && a.dwVolumeSerialNumber==b.dwVolumeSerialNumber && a.nFileIndexHigh==b.nFileIndexHigh && a.nFileIndexLow==b.nFileIndexLow;
    if(original!=INVALID_HANDLE_VALUE) CloseHandle(original);
    if(alias!=INVALID_HANDLE_VALUE) CloseHandle(alias);
    return same?result:QString();
}
inline int failure(int code,const char* reason) {std::fprintf(stderr,"native fixture %d: %s (Win32=%lu)\n",code,reason,GetLastError());return code;}
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
        if(QDir::currentPath()!=sandbox || environment(L"PATH")!=shortDirectory(sandbox)+";"+qtDirectory()) {error="private CWD/PATH mismatch";return false;}
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
            FILETIME created{},ended{},kernel{},user{};
            const bool times=GetProcessTimes(process,&created,&ended,&kernel,&user);
            const quint64 started=(quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime;
            const bool own=times && record.value("started").toString()==QString::number(started);
            const DWORD wait=own?WaitForSingleObject(process,1500):WAIT_FAILED;DWORD exitCode=STILL_ACTIVE;
            const bool exited=GetExitCodeProcess(process,&exitCode) && exitCode==0;CloseHandle(process);
            if(!own || wait!=WAIT_OBJECT_0 || !exited) {error=QString("owned helper identity=%1 wait=%2 exit=%3").arg(own).arg(wait).arg(exitCode);return false;}
        } else if(GetLastError()!=ERROR_INVALID_PARAMETER) {error="cannot verify helper termination";return false;}
        return QFile::remove(capture);
    }
};

#ifdef GLANCELOT_TEST_LAUNCH_STUB
inline int bootstrap(const QStringList& originalArguments) {
    QTemporaryDir temp(QDir::tempPath()+QString::fromUtf8("/glancelot native \U0001f9ea space-XXXXXX"));
    if(!temp.isValid()) return failure(101,"temporary directory");
    const QString sandbox=canonical(temp.path()), runner=QDir(sandbox).filePath("platform_integration_tests.exe");
    const QString stub=QString::fromUtf8(GLANCELOT_TEST_LAUNCH_STUB);
    if(hash(stub).isEmpty() || !QFile::copy(QCoreApplication::applicationFilePath(),runner) || !QDir(sandbox).mkdir("captures")) return failure(102,"copy runner or capture directory");
    for(const auto* name:{"explorer.exe","wt.exe","powershell.exe"}) if(!QFile::copy(stub,QDir(sandbox).filePath(name))) return failure(103,"copy helper");
    QProcessEnvironment env=QProcessEnvironment::systemEnvironment();
    const QString searchDirectory=shortDirectory(sandbox);
    if(searchDirectory.isEmpty() || qtDirectory().isEmpty()) return failure(110,"ASCII 8.3 directory alias unavailable, identity mismatch or loaded Qt module missing");
    env.insert("PATH",searchDirectory+";"+qtDirectory());env.insert("GLANCELOT_NATIVE_SANDBOX",sandbox);
    env.insert("GLANCELOT_NATIVE_QT_DIR",qtDirectory());env.insert("GLANCELOT_NATIVE_RUNNER_SHA",hash(runner));
    env.insert("GLANCELOT_NATIVE_STUB_SHA",hash(stub));env.insert("GLANCELOT_NATIVE_TOKEN",QUuid::createUuid().toString());
    // Resolve relative QtTest log destinations before changing the child's working directory.
    QStringList args=originalArguments;
    // Give the console-less copied runner a real default QtTest sink. Replay it
    // through the parent's stdout so CTest retains the complete result.
    const QString defaultLog=QDir(sandbox).filePath("test-output.txt");
    const bool ownLog=!args.contains("-o");

    for(qsizetype i=1;i<args.size();++i) if(args[i-1]=="-o" && args[i]!="-" && !args[i].startsWith("-,")) {
        const qsizetype comma=args[i].lastIndexOf(','); const QString file=comma>=0?args[i].left(comma):args[i];
        if(QDir::isRelativePath(file)) args[i]=QFileInfo(file).absoluteFilePath()+(comma>=0?args[i].mid(comma):QString());
    }
    // QtTest opens its logger with a narrow CRT filename. Resolve this simple
    // relative name inside the unchanged wide-character private CWD.
    if(ownLog) args.append({"-o","test-output.txt,txt"});
    HANDLE job=CreateJobObjectW(nullptr,nullptr); if(!job) return failure(104,"create owned job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit))) {CloseHandle(job);return failure(105,"configure owned job");}
    QProcess child;child.setProcessEnvironment(env);child.setWorkingDirectory(sandbox);child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arg){arg->flags|=CREATE_SUSPENDED|CREATE_NO_WINDOW;});
    child.start(runner,args.mid(1));
    if(!child.waitForStarted(2000)) {CloseHandle(job);return failure(106,"start copied runner");}
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
    if(!assigned || !resumed) {child.kill();child.waitForFinished(1000);CloseHandle(job);return failure(107,"assign or resume owned runner");}
    const bool finished=child.waitForFinished(22000);
    if(!finished) {TerminateJobObject(job,108);child.waitForFinished(1000);}
    int result=finished && child.exitStatus()==QProcess::NormalExit?child.exitCode():108;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    QElapsedTimer settle;settle.start();
    while(QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr) && accounting.ActiveProcesses && settle.elapsed()<1000) QThread::msleep(5);
    if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr) || accounting.ActiveProcesses!=0) {
        if(result==0) result=109; // An owned helper outlived the test child: never silently accept a leak.
        TerminateJobObject(job,109);
    }
    // Closing this handle also kills any owned helper that outlived its request, including abnormal child exits.
    CloseHandle(job);
    if(ownLog) {
        QFile log(defaultLog);
        if(log.open(QIODevice::ReadOnly)) {
            const bool truncated=log.size()>256*1024;
            auto bytes=log.read(256*1024);bytes.replace("\r\n","\n");
            std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);
            if(truncated) {std::fprintf(stderr,"native fixture: QtTest log truncated at 256 KiB\n");if(result==0) result=111;}
        } else if(result==0) result=failure(111,"missing default QtTest log");
    }
    if(result) std::fprintf(stderr,"native fixture: copied runner exit/result %d\n",result);
    return result;
}
#endif
} // namespace native_fixture
#endif
