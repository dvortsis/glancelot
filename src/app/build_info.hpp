#pragma once
// REL-01: this build's provenance (docs/RELEASING.md "Build provenance"): the semantic version, the full Git SHA (the short
// SHA for display), one UTC ISO 8601 build time and the build kind. The values come from one generated translation unit, so
// a new commit recompiles that file only. About shows them through the BuildInfo QML singleton (import Glancelot), which
// this unit registers itself at application start; --version prints versionText().
#include <QObject>
#include <QString>

namespace glancelot::build {
inline constexpr qsizetype shortShaLength = 7;

struct Info {
    QString version;  // X.Y.Z, or X.Y.Z-rc.N / X.Y.Z-dev.N
    QString sha;      // full lowercase hex, or "unknown"
    QString builtAt;  // YYYY-MM-DDTHH:MM:SSZ
    QString kind;     // release, rc, dev or local
    QString shortSha() const;
};

// This build's values.
const Info& current();
// The --version text: "Glancelot <version> (<short SHA>)", then "commit: ", "built: " and "kind: " lines, each ending in \n.
QString versionText(const Info& info);
QString versionText();
}

// Read-only QML view of glancelot::build::current().
class BuildInfo final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString sha READ sha CONSTANT)
    Q_PROPERTY(QString shortSha READ shortSha CONSTANT)
    Q_PROPERTY(QString builtAt READ builtAt CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString details READ details CONSTANT)
public:
    explicit BuildInfo(QObject* parent = nullptr);
    QString version() const { return glancelot::build::current().version; }
    QString sha() const { return glancelot::build::current().sha; }
    QString shortSha() const { return glancelot::build::current().shortSha(); }
    QString builtAt() const { return glancelot::build::current().builtAt; }
    QString kind() const { return glancelot::build::current().kind; }
    QString details() const { return glancelot::build::versionText(); } // what --version prints, for copying from About
};
