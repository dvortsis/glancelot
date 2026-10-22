#pragma once
#include "startup_intent.hpp"
#include <QByteArray>

namespace glancelot::startup {
struct MaintenanceResult {QByteArray json;int exitCode{};};
// Runs with QCoreApplication only and explicit context. No viewer, window claim,
// themes, layouts or QML are constructed. Caller writes the bounded JSON locally.
MaintenanceResult maintenance(const Intent&,const recovery::Context&,recovery::Options = {});
QString codeName(recovery::Code);
int exitCode(recovery::Code);
}
