// The platform TU sees the real Shell declarations before this single typed seam.
#include "platform_fixture_capture.hpp"
namespace platform_fixture {
State& state(){static State value;return value;}
std::optional<QJsonObject> captureArguments(const QStringList& arguments,const QString& directory) {
    auto& capture=state();const auto nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto destination=QDir(capture.captureRoot).filePath("capture-"+nonce+".json");
    QProcess child;auto env=QProcessEnvironment::systemEnvironment();
    env.insert("GLANCELOT_TEST_ARGV_ROOT",capture.captureRoot);child.setProcessEnvironment(env);
    child.setWorkingDirectory(directory);child.start(helperProgram(),QStringList{nonce}+arguments);
    if(!child.waitForStarted(2000)||!child.waitForFinished(2000)) {
        child.kill();child.waitForFinished(1000);capture.error="Native argument helper did not finish.";return {};
    }
    if(child.exitStatus()!=QProcess::NormalExit||child.exitCode()!=0) {
        capture.error=QString("Native argument helper exited %1.").arg(child.exitCode());return {};
    }
    QFile file(destination);
    if(!file.open(QIODevice::ReadOnly)||file.size()>256*1024){capture.error="Missing or oversized native capture.";return {};}
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.read(256*1024+1),&error);file.close();
    if(error.error!=QJsonParseError::NoError||!document.isObject()){capture.error="Invalid native capture.";return {};}
    const auto record=document.object();
    if(record.value("arguments").toArray()!=QJsonArray::fromStringList(arguments)||
       record.value("token").toString()!=nonce||!samePath(record.value("module").toString(),helperProgram())||
       !samePath(record.value("cwd").toString(),directory.isEmpty()?QDir::currentPath():directory)||record.value("console").toBool(true)) {
        capture.error="Native argv, module, CWD or console mismatch.";return {};
    }
    if(!QFile::remove(destination)){capture.error="Cannot remove generated capture.";return {};}
    return record;
}
bool Process::startDetached(const QString& program,const QStringList& arguments,const QString& directory) {
    auto& capture=state();capture.launches.append({program,arguments,directory});
    if(!capture.launchResults.empty()) {
        const bool result=capture.launchResults.front();capture.launchResults.pop_front();if(!result)return false;
    }
    // Requested OS launcher names remain inert data; execute only the friendly peer.
    const auto record=captureArguments(arguments,directory);
    if(!record)return false;capture.nativeRecords.append(*record);return true;
}
QString Paths::findExecutable(const QString& name) {
    auto& capture=state();++capture.lookupCalls;
    if(name!="wt.exe"){capture.error="Unexpected adapter lookup.";return {};}
    return capture.terminalExecutable;
}
HRESULT WINAPI captureSelection(PCIDLIST_ABSOLUTE folder,UINT count,PCUITEMID_CHILD_ARRAY children,DWORD flags) {
    auto& state=platform_fixture::state().shell;++state.calls;state.count=count;state.flags=flags;state.children.clear();state.folder.clear();
    PWSTR text=nullptr;
    if(SUCCEEDED(SHGetNameFromIDList(folder,SIGDN_FILESYSPATH,&text))) {state.folder=QString::fromWCharArray(text);CoTaskMemFree(text);}
    for(UINT i=0;i<count && i<64;++i) {
        PIDLIST_ABSOLUTE combined=ILCombine(folder,children[i]);text=nullptr;
        if(combined && SUCCEEDED(SHGetNameFromIDList(combined,SIGDN_FILESYSPATH,&text))) {state.children.append(QString::fromWCharArray(text));CoTaskMemFree(text);}
        ILFree(combined); // Own only the combined PIDL; every argument remains borrowed.
    }
    return state.result;
}
}
#define QProcess platform_fixture::Process
#define QStandardPaths platform_fixture::Paths
#define SHOpenFolderAndSelectItems platform_fixture::captureSelection
#include "../src/app/platform_integration.cpp"
#undef SHOpenFolderAndSelectItems

#undef QStandardPaths
#undef QProcess
