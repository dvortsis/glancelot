#pragma once
#include "fs_revision.hpp"
#include <QCryptographicHash>

namespace fs_watch {
// Private worker/controller protocol r1. This hashes native metadata, not media
// content. A later external write still requires event/poll reconciliation.
inline QString sourceToken(const Revision& revision) {
    if(!revision.regular||!revision.changed||revision.directory||revision.identity().isEmpty())return {};
    auto bytes=QByteArrayLiteral("Glancelot.SourceRevision");
    bytes.append(char(0));bytes.append(char(1));
    const auto append=[&](std::uint64_t value,unsigned count) {
        for(unsigned byte=0;byte<count;++byte)bytes.append(char((value>>(byte*8))&0xff));
    };
    for(const auto value:{revision.volume,revision.object,revision.born,revision.size,revision.modified,revision.changed})append(value,8);
    append(revision.links,4);
    bytes.append(char(revision.known));bytes.append(char(revision.directory));bytes.append(char(revision.link));
    return QStringLiteral("r1:")+QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
// Filesystem work: call only in an isolated worker, never on the GUI thread.
inline QString probeSourceToken(const QString& path) { return sourceToken(probe(native(path))); }
inline bool validSourceToken(const QString& token) {
    if(token.size()!=67||!token.startsWith(QLatin1String("r1:")))return false;
    for(qsizetype i=3;i<token.size();++i) {
        const auto value=token[i].unicode();
        if(!((value>='0'&&value<='9')||(value>='a'&&value<='f')))return false;
    }
    return true;
}
}
