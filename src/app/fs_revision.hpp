#pragma once
// FS-WATCH-01 private worker-only identity/revision probes. Never call on the GUI thread.
#include <QString>
#include <QDir>
#include <QHash>
#include <QSet>
#include <filesystem>
#include <stop_token>
#include <cstdint>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#endif

namespace fs_watch {
inline std::filesystem::path native(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}
inline QString display(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
#else
    return QString::fromUtf8(path.c_str());
#endif
}
inline QString key(const QString& path) {
    auto result=QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    result=result.toCaseFolded();
#endif
    return result;
}
struct Revision {
    std::uint64_t volume{}, object{}, size{}, modified{}, changed{}, born{};
    std::uint32_t links{};
    bool known{}, directory{}, link{};
    bool operator==(const Revision&) const = default;
    QString identity() const {
        if(!known || !object || !born || link || (!directory && links!=1))return {};
        return QString::number(volume,16)+':'+QString::number(object,16)+':'+QString::number(born,16);
    }
};
inline Revision probe(const std::filesystem::path& path) {
    Revision result;
#ifdef Q_OS_WIN
    const HANDLE handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return result;
    BY_HANDLE_FILE_INFORMATION info{};
    if(GetFileInformationByHandle(handle,&info)) {
        result.known=true;result.volume=info.dwVolumeSerialNumber;
        result.object=(std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
        result.size=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
        result.modified=(std::uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
        result.born=(std::uint64_t(info.ftCreationTime.dwHighDateTime)<<32)|info.ftCreationTime.dwLowDateTime;
        result.links=info.nNumberOfLinks;
        result.directory=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        result.link=(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
    }
    CloseHandle(handle);
#else
    struct stat info{};
    if(::lstat(path.c_str(),&info)==0) {
        result.known=true;result.volume=std::uint64_t(info.st_dev);result.object=std::uint64_t(info.st_ino);
        result.size=std::uint64_t(info.st_size);result.links=std::uint32_t(info.st_nlink);
        result.modified=std::uint64_t(info.st_mtim.tv_sec)*1000000000ULL+std::uint64_t(info.st_mtim.tv_nsec);
        result.changed=std::uint64_t(info.st_ctim.tv_sec)*1000000000ULL+std::uint64_t(info.st_ctim.tv_nsec);
        result.directory=S_ISDIR(info.st_mode);result.link=S_ISLNK(info.st_mode);
#ifdef Q_OS_LINUX
        struct statx extended{};
        if(::statx(AT_FDCWD,path.c_str(),AT_SYMLINK_NOFOLLOW,STATX_BTIME,&extended)==0&&(extended.stx_mask&STATX_BTIME))
            result.born=std::uint64_t(extended.stx_btime.tv_sec)*1000000000ULL+std::uint64_t(extended.stx_btime.tv_nsec);
#endif
    }
#endif
    return result;
}
struct Entry { QString path; Revision revision; bool operator==(const Entry&) const = default; };
inline constexpr std::size_t IdentityByteBudget=16*1024*1024;
inline std::size_t entryBytes(const QString& path) {
    // Both UTF-16 path/key spellings plus conservative node/string allocator cost.
    return std::size_t(path.size())*4+sizeof(Entry)+128;
}
struct Snapshot {
    QString directory;
    Revision revision;
    QHash<QString,Entry> entries;
    bool complete{}, missing{}, cancelled{};
};
// A partial/unreadable snapshot is never evidence that an entry was deleted.
// Limit counts all directory entries, including unsupported files and links.
inline Snapshot snapshot(const QString& directory, std::stop_token stop={}, std::size_t limit=20000,
                         std::size_t byteLimit=IdentityByteBudget) {
    Snapshot result;result.directory=directory;
    if(stop.stop_requested()){result.cancelled=true;return result;}
    const auto path=native(directory);result.revision=probe(path);
    std::error_code error;
    std::filesystem::directory_iterator it(path,error),end;
    if(error) {result.missing=error==std::errc::no_such_file_or_directory;return result;}
    bool readable=result.revision.known&&result.revision.directory&&!result.revision.link;
    std::size_t count=0,bytes=0;
    for(;it!=end;it.increment(error)) {
        if(error)return result;
        if(stop.stop_requested()){result.cancelled=true;return result;}
        if(count++==limit)return result;
        const auto name=display(it->path());const auto cost=entryBytes(name);
        if(cost>byteLimit-bytes)return result;
        bytes+=cost;const auto revision=probe(it->path());
        readable=readable&&revision.known;
        result.entries.insert(key(name),Entry{name,revision});
    }
    result.complete=readable&&!error&&!stop.stop_requested();
    result.cancelled=stop.stop_requested();
    return result;
}
// Match only unique native identities in two complete snapshots. Hard links,
// reparse points, unknown identities and truncated observations are ambiguous.
inline QHash<QString,QString> renames(const Snapshot& before,const Snapshot& after) {
    QHash<QString,QString> result;
    if(!before.complete||!after.complete)return result;
    QHash<QString,QString> oldNames,newNames;QSet<QString> ambiguous;
    auto collect=[&](const auto& entries,auto& names) {
        for(const auto& entry:entries) {
            const auto id=entry.revision.identity();if(id.isEmpty())continue;
            if(names.contains(id))ambiguous.insert(id);else names.insert(id,entry.path);
        }
    };
    collect(before.entries,oldNames);collect(after.entries,newNames);
    for(auto it=oldNames.cbegin();it!=oldNames.cend();++it) {
        if(!ambiguous.contains(it.key())&&newNames.contains(it.key())&&key(it.value())!=key(newNames[it.key()]))
            result.insert(key(it.value()),newNames[it.key()]);
    }
    return result;
}
}
