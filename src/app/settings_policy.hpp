#pragma once
#include "glancelot/settings_recovery.hpp"
#include <QJsonObject>

namespace glancelot::recovery::policy {
// A pure schema merge, NOT slot authorization. The facade must hold the proper
// sessions guard and bind this scope to the immutable plan before applying it.
struct Selection {
    GlobalSection global{GlobalSection::All};
    std::optional<SlotSection> slotSection;
    int slot{};
};
struct Result {
    Status status;
    std::optional<QJsonObject> replacement;
    QList<QStringList> ownedPaths;
    QStringList invalidPath;
};
Result merge(const QJsonObject& current,const QJsonObject& source,Selection,Options = {});
QString revision(); // shipped known-field policy + registry + theme/layout data
}
