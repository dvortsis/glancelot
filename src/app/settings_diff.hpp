#pragma once
#include "glancelot/settings_recovery.hpp"
#include <QJsonObject>

namespace glancelot::recovery::diff {
struct Result {
    Status status;
    QList<DiffRow> rows;
    bool truncated{};
    qint64 accountedBytes{}; // conservative encoded/in-memory report accounting
};
// current/source/replacement are bounded validated JSON values. Applied rows
// compare current to replacement; other source differences are preserved/unapplied.
// This function cannot construct or authorize an applicable RestorePlan.
Result build(const QJsonObject& current,const QJsonObject& source,
             const QJsonObject& replacement,const QList<QStringList>& ownedPaths,Options = {});
}
