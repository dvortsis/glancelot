// Test-only decoder peer. Generated pixels and bounded control data; no media IO.
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedMemory>
#include <QThread>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/app/transfer_segment.hpp"

static bool send(const QJsonObject& object) {
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
    return std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0;
}
static QJsonObject receive() {
    char buffer[8194]{};
    if(!std::fgets(buffer,sizeof(buffer),stdin))return {};
    const QByteArray bytes(buffer);
    if(!bytes.endsWith('\n'))return {};
    return QJsonDocument::fromJson(bytes).object();
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()<4||args[1]!="--fixture-config"||!QFileInfo(args[2]).isAbsolute())return 2;
    QFile control(args[2]);
    if(!control.open(QIODevice::ReadOnly)||control.size()>8192)return 2;
    const auto controls=control.read(8193);if(controls.size()>8192)return 2;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(controls,&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())return 2;
    const bool revision=args[3]=="--source-revision";
    if(args.size()!=(revision?5:4))return 2;
    const auto file=args.last();if(!QFileInfo(file).isAbsolute())return 2;
    auto config=document.object();
    const auto overrides=config.value("files").toObject().value(QFileInfo(file).fileName()).toObject();
    for(auto it=overrides.begin();it!=overrides.end();++it)config[it.key()]=it.value();
    const auto token=config.value("sourceToken").isUndefined()?QJsonValue("r1:"+QString(64,'a')):config.value("sourceToken");
    auto pause=[&](const char* key){QThread::msleep(unsigned(qBound(0,config.value(key).toInt(),60000)));};
    if(revision) {
        pause("revisionDelayMs");QJsonObject reply{{"phase","revision"}};
        if(!config.value("omitRevisionToken").toBool())reply["sourceToken"]=config.contains("revisionToken")?config.value("revisionToken"):token;
        return send(reply)?0:3;
    }
    const int width=config.value("width").toInt(32),height=config.value("height").toInt(24);
    if(!send({{"phase","probe"},{"width",width},{"height",height},{"bytes",width*height*4}}))return 3;
    const auto key=receive().value("key").toString();if(key.isEmpty())return 3;
    // STACK-CONFIRM R4-B1: "dieAt" ends this process abruptly, as a kill or a crash would, at one point of the transfer. It first
    // takes the lock the old QSharedMemory transport shared with the viewer and never gives it back, so a viewer that goes back
    // to that transport freezes in this fixture's tests. A viewer on TransferSegment never looks at that lock. "holdLock" takes
    // the same lock and carries on, for a process the viewer kills itself. "markerDir" records the key, so a test can check the
    // memory is really gone, and that this point was reached.
    QSharedMemory legacy(key);
    const auto holdLegacyLock=[&]{legacy.attach();legacy.lock();};
    const auto dieAt=[&](const char* point){if(config.value("dieAt").toString()==QLatin1String(point)){holdLegacyLock();std::_Exit(9);}};
    glancelot::detail::TransferSegment memory(key);if(!memory.attach())return 3;
    if(config.contains("markerDir")) {
        QFile marker(config.value("markerDir").toString()+"/"+key);
        if(!marker.open(QIODevice::WriteOnly)||marker.write(file.toUtf8())<=0)return 3;
    }
    dieAt("attached");
    if(config.value("holdLock").toBool())holdLegacyLock();
    pause("decodeDelayMs");
    QImage image(width,height,QImage::Format_RGBA8888);
    image.fill(QColor(config.value("color").toString("red")));
    if(memory.size()<image.sizeInBytes())return 3;
    // Parent owns admission and reads only after this completion frame.
    std::memcpy(memory.data(),image.constBits(),size_t(image.sizeInBytes()));
    QJsonObject reply{{"phase","decoded"},{"width",width},{"height",height},{"stride",width*4},{"color","sRGB fixture"}};
    if(!config.value("omitSourceToken").toBool())reply["sourceToken"]=token;
    if(!send(reply))return 3;
    char ack[16]{};const bool acknowledged=std::fgets(ack,sizeof(ack),stdin);
    dieAt("acknowledged");
    return acknowledged?0:3;
}
