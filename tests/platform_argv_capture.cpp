// Test-only native argv peer. No shell, process creation, self-copy or event loop.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()<2||args.size()>66)return 91;
    const auto token=args[1];const QUuid nonce(token);
    if(nonce.isNull()||nonce.toString(QUuid::WithoutBraces)!=token)return 91;
    const auto root=QFileInfo(qEnvironmentVariable("GLANCELOT_TEST_ARGV_ROOT")).canonicalFilePath();
    const auto temporary=QDir(QDir::tempPath()).canonicalPath();
    if(root.isEmpty()||temporary.isEmpty()||!QFileInfo(root).fileName().startsWith("glancelot-argv-")||
       QDir(QFileInfo(root).absolutePath()).canonicalPath()!=temporary)return 91;
    qint64 units=0;for(const auto& arg:args.mid(2))units+=arg.size();if(units>20000)return 92;
    bool console=false;
#ifdef Q_OS_WIN
    console=GetConsoleWindow()!=nullptr;
#endif
    const auto bytes=QJsonDocument(QJsonObject{{"arguments",QJsonArray::fromStringList(args.mid(2))},
        {"cwd",QDir::currentPath()},{"module",QCoreApplication::applicationFilePath()},
        {"token",token},{"console",console}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>256*1024)return 92;
    QFile output(QDir(root).filePath("capture-"+token+".json"));
    if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 93;
    return output.write(bytes)==bytes.size()&&output.flush()?0:94;
}
