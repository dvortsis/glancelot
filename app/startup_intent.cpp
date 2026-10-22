#include "startup_intent.hpp"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace glancelot::startup {
namespace {
const QStringList viewerOptions{"quit-after","screenshot","perf-report","report","theme","theme-mode","show-grid","show-settings","show-themes","frameless","reset-settings","no-resume","window-size","safe-mode"};
const QStringList maintenanceOptions{"settings-backup","settings-diff","settings-restore"};
const QStringList selectionOptions{"settings-scope","settings-slot","settings-source","settings-preview"};
// QGuiApplication used to consume these before the viewer parser. Preserve
// their separate-operand spelling while validating before GUI construction.
const QStringList qtValues{"platform","platformpluginpath","platformtheme","plugin","qwindowgeometry","qwindowtitle","qwindowicon","session","display","geometry","title","icon","name","visual","ncols","qmljsdebugger"};
const QStringList qtFlags{"reverse","nograb","dograb","sync"};
bool path(const QString& text) {return !text.isEmpty()&&!text.startsWith('-')&&!text.contains(QChar(0))&&text.size()<=32768;}
}
void configure(QCommandLineParser& parser) {
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.setApplicationDescription("A local-first native photo viewer. JPEG/PNG alpha.");
    parser.addHelpOption();parser.addVersionOption();
    parser.addPositionalArgument("image","Image to open.","[image]");
    parser.addOption({"quit-after","Exit after the given milliseconds (test automation).","milliseconds"});
    parser.addOption({"screenshot","Save a canvas/window screenshot before timed exit.","path"});
    parser.addOption({"perf-report","Write a bounded, redacted local performance report on quit.","path"});
    parser.addOption({"report","Write local JSON startup/render evidence before timed exit.","path"});
    parser.addOption({"theme","Select a built-in family (saved preference).","id"});
    parser.addOption({"theme-mode","Select system, dark, or light (saved preference).","mode"});
    parser.addOption({"show-grid","Open the folder grid at startup (test automation)."});
    parser.addOption({"show-settings","Open settings at startup (test automation)."});
    parser.addOption({"show-themes","Open the theme picker at startup (test automation)."});
    parser.addOption({"frameless","Start in the frameless window mode (test automation)."});
    parser.addOption({"reset-settings","Set damaged or incompatible settings aside and start with defaults."});
    parser.addOption({"no-resume","Do not claim a slot or remember/restore browsing position."});
    parser.addOption({"window-size","Initial logical window size, e.g. 540x360.","size"});
    parser.addOption({"safe-mode","Use isolated temporary settings and request a software RHI device."});
    parser.addOption({"settings-backup","Capture exact settings bytes into a new sealed directory.","new-directory"});
    parser.addOption({"settings-diff","Preview selected settings from a sealed package.","package"});
    parser.addOption({"settings-restore","Apply a previously previewed settings package.","package"});
    parser.addOption({"settings-scope","Select global/viewing/browsing/appearance/bindings/performance/position/layout.","scope"});
    parser.addOption({"settings-slot","Select an offline position/layout slot (1..1000000).","slot"});
    parser.addOption({"settings-source","Select primary or last-good package member.","member"});
    parser.addOption({"settings-preview","Required sf1 fingerprint from settings-diff before restore.","fingerprint"});
    for(const auto& name:qtValues+qtFlags) {
        QCommandLineOption option(name,"Qt GUI option.",qtValues.contains(name)?QStringLiteral("value"):QString{});
        option.setFlags(QCommandLineOption::HiddenFromHelp);parser.addOption(option);
    }
}
Intent parse(const QStringList& arguments,QCommandLineParser& parser) {
    Intent result;
    const auto fail=[&](const QString& message){result.mode=Mode::Error;result.error=message;return result;};
    if(arguments.isEmpty())return fail("Missing program name.");
    if(!parser.parse(arguments))return fail(parser.errorText());
    QSet<QString> seen;
    for(auto name:parser.optionNames()) {
        if(name=="h"||name=="help-all")name="help";if(name=="v")name="version";
        if(name=="geometry")name="qwindowgeometry";if(name=="title")name="qwindowtitle";if(name=="icon")name="qwindowicon";
        if(seen.contains(name))return fail("Repeated option: "+name);seen.insert(name);
    }
    bool terminated=false;
    for(const auto& argument:arguments.sliced(1)) {
        if(argument=="--"){terminated=true;continue;}if(terminated)continue;
        for(const auto& name:qtValues)if(name!="qmljsdebugger"&&(argument.startsWith("--"+name+"=")||argument.startsWith("-"+name+"=")))
            return fail("Qt GUI options require a separate operand: "+name);
        if(argument=="-qmljsdebugger"||argument=="--qmljsdebugger")return fail("Qt QML debugging requires an inline operand.");
    }
    int verbs=0;QString verb;
    for(const auto& name:maintenanceOptions)if(parser.isSet(name)){++verbs;verb=name;}
    if(verbs>1)return fail("Choose exactly one settings maintenance verb.");
    const bool help=parser.isSet("help")||parser.isSet("help-all"),version=parser.isSet("version");
    if(help||version) {
        if((help&&version)||verbs||!parser.positionalArguments().isEmpty())return fail("Help/version cannot be combined with a command.");
        for(const auto& name:viewerOptions+selectionOptions+qtValues+qtFlags)if(parser.isSet(name))return fail("Help/version cannot be combined with options.");
        result.mode=help?Mode::Help:Mode::Version;return result;
    }
    if(verbs) {
        if(!parser.positionalArguments().isEmpty())return fail("Settings maintenance does not accept an image.");
        for(const auto& name:viewerOptions+qtValues+qtFlags)if(parser.isSet(name))return fail("Settings maintenance cannot use --"+name+".");
        const auto operand=parser.value(verb);if(!path(operand))return fail("Invalid or ambiguous package directory.");
        result.packageDirectory=QDir::cleanPath(QFileInfo(operand).absoluteFilePath());
        result.mode=verb=="settings-backup"?Mode::Backup:verb=="settings-diff"?Mode::Diff:Mode::Restore;
        if(result.mode==Mode::Backup) {
            for(const auto& name:selectionOptions)if(parser.isSet(name))return fail("Backup does not accept selection or preview options.");
            return result;
        }
        const auto scope=parser.isSet("settings-scope")?parser.value("settings-scope"):QStringLiteral("global");
        const QStringList globals{"global","viewing","browsing","appearance","bindings","performance"};
        const auto index=globals.indexOf(scope);
        if(index>=0)result.global=static_cast<recovery::GlobalSection>(index);
        else if(scope=="position"||scope=="layout")result.slotSection=scope=="position"?recovery::SlotSection::Position:recovery::SlotSection::Layout;
        else return fail("Unknown settings scope.");
        if(bool(result.slotSection)!=parser.isSet("settings-slot"))return fail("Position/layout requires a slot; global sections do not accept one.");
        if(result.slotSection) {
            const auto text=parser.value("settings-slot");bool ok=false;result.slot=text.toInt(&ok);
            if(!ok||result.slot<1||result.slot>1000000||QString::number(result.slot)!=text)return fail("Invalid settings slot.");
        }
        if(parser.isSet("settings-source")) {
            const auto source=parser.value("settings-source");
            if(source!="primary"&&source!="last-good")return fail("Unknown settings source member.");
            result.source=source=="primary"?recovery::SourceMember::Primary:recovery::SourceMember::LastGood;
        }
        if(result.mode==Mode::Diff&&parser.isSet("settings-preview"))return fail("Diff does not accept a preview fingerprint.");
        if(result.mode==Mode::Restore) {
            result.expectedFingerprint=parser.value("settings-preview");
            static const QRegularExpression fingerprint("\\Asf1:[0-9a-f]{64}\\z");
            if(!parser.isSet("settings-preview")||!fingerprint.match(result.expectedFingerprint).hasMatch())return fail("Restore requires an exact sf1 preview fingerprint.");
        }
        return result;
    }
    for(const auto& name:selectionOptions)if(parser.isSet(name))return fail("Orphan settings selection option: "+name);
    result.safeMode=parser.isSet("safe-mode");result.noResume=parser.isSet("no-resume");
    if(result.safeMode&&parser.isSet("reset-settings"))return fail("Safe mode cannot reset original settings.");
    if(parser.positionalArguments().size()>1)return fail("Only one image may be opened.");
    if(!parser.positionalArguments().isEmpty()) {
        const auto image=parser.positionalArguments().first();
        if(image.isEmpty()||image.contains(QChar(0))||image.size()>32768)return fail("Invalid image operand.");
    }
    if(parser.isSet("quit-after")) {
        bool ok=false;const auto duration=parser.value("quit-after").toInt(&ok);
        if(!ok||duration<100||duration>3600000)return fail("Invalid quit-after duration.");
    }
    for(const auto* option:{"screenshot","perf-report","report"})if(parser.isSet(option)&&!path(parser.value(option)))return fail("Invalid output path.");
    if(parser.isSet("theme-mode")&&!QStringList{"system","dark","light"}.contains(parser.value("theme-mode")))return fail("Invalid theme mode.");
    if(parser.isSet("window-size")) {
        const auto parts=parser.value("window-size").split('x');
        if(parts.size()!=2)return fail("Invalid window size.");
        for(const auto& part:parts){bool ok=false;const auto value=part.toInt(&ok);if(!ok||value<=0)return fail("Invalid window size.");}
    }
    result.mode=Mode::Viewer;return result;
}
QStringList originalArguments(int argc,char** argv) {
    QStringList result;
#ifdef Q_OS_WIN
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!arguments)return result;
    for(int index=0;index<count;++index)result.append(QString::fromWCharArray(arguments[index]));
    LocalFree(arguments);
#else
    for(int index=0;index<argc;++index)result.append(QString::fromLocal8Bit(argv[index]));
#endif
    return result;
}
}
