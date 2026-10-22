#pragma once
#include <QCommandLineParser>
#include <QStringList>
#include "glancelot/settings_recovery.hpp"

namespace glancelot::startup {
enum class Mode { Viewer, Help, Version, Backup, Diff, Restore, Error };
struct Intent {
    Mode mode{Mode::Error};
    QString error,packageDirectory,expectedFingerprint;
    recovery::GlobalSection global{recovery::GlobalSection::All};
    std::optional<recovery::SlotSection> slotSection;
    int slot{};
    recovery::SourceMember source{recovery::SourceMember::Primary};
    bool safeMode{},noResume{};
    bool maintenance() const {return mode==Mode::Backup||mode==Mode::Diff||mode==Mode::Restore;}
};
// The same definitions and parser instance serve early validation and the viewer.
// parse never exits, constructs an application, or reads/writes preferences.
void configure(QCommandLineParser&);
Intent parse(const QStringList& arguments,QCommandLineParser&);
QStringList originalArguments(int argc,char** argv);
}
