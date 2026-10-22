#pragma once
#include "contracts.hpp"
#include <QJsonObject>
#include <QString>
#include <span>

namespace glancelot {
// SAFE-MODE-01: one shipped definition, shared by the controller and headless
// settings validation. No viewer, preference read/write, or GUI construction.
std::span<const Command> commandRegistry();
struct BindingResolution {
    bool valid{};
    QJsonObject effective; // known IDs only; callers preserve unknown stored IDs
    QString error;
};
// Preserve existing loading semantics, including user choices winning over a
// newly introduced conflicting default. Recovery separately rejects invalid
// selected source types before applying this shared effective-map validation.
BindingResolution resolveCommandBindings(const QJsonObject& stored);
}
