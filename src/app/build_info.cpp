#include "build_info.hpp"
#include "build_info_values.hpp"
#include <QCoreApplication>
#include <QtQml/qqml.h>

namespace glancelot::build {
QString Info::shortSha() const {
    return sha == QLatin1String("unknown") ? sha : sha.left(shortShaLength);
}

const Info& current() {
    static const Info info{QString::fromLatin1(generated::version), QString::fromLatin1(generated::sha),
                           QString::fromLatin1(generated::builtAt), QString::fromLatin1(generated::kind)};
    return info;
}

QString versionText(const Info& info) {
    return QStringLiteral("Glancelot %1 (%2)\ncommit: %3\nbuilt: %4\nkind: %5\n")
        .arg(info.version, info.shortSha(), info.sha, info.builtAt, info.kind);
}

QString versionText() { return versionText(current()); }
}

BuildInfo::BuildInfo(QObject* parent) : QObject(parent) {}

namespace {
// Registered here, not in app/main.cpp, so the app and every test that links this unit see the same type
// (as SelectionExportService does).
void registerBuildInfo() {
    qmlRegisterSingletonType<BuildInfo>("Glancelot", 1, 0, "BuildInfo",
                                        [](QQmlEngine*, QJSEngine*) -> QObject* { return new BuildInfo; });
}
Q_COREAPP_STARTUP_FUNCTION(registerBuildInfo)
}
