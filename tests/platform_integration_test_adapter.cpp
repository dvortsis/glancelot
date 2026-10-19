// The platform TU sees the real Shell declarations before this single typed seam.
#include "native_platform_fixture.hpp"
namespace native_fixture {
ShellCapture& shellCapture() {static ShellCapture state;return state;}
HRESULT WINAPI captureSelection(PCIDLIST_ABSOLUTE folder,UINT count,PCUITEMID_CHILD_ARRAY children,DWORD flags) {
    auto& state=shellCapture();++state.calls;state.count=count;state.flags=flags;state.children.clear();state.folder.clear();
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
#define SHOpenFolderAndSelectItems native_fixture::captureSelection
#include "../src/app/platform_integration.cpp"
#undef SHOpenFolderAndSelectItems
