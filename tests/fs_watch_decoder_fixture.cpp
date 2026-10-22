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
#include <cstring>

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
    if(!send({{"phase","probe"},{"width",32},{"height",24},{"bytes",3072}}))return 3;
    const auto key=receive().value("key").toString();if(key.isEmpty())return 3;
    QSharedMemory memory(key);if(!memory.attach())return 3;
    pause("decodeDelayMs");
    QImage image(32,24,QImage::Format_RGBA8888);
    image.fill(QColor(config.value("color").toString("red")));
    if(memory.size()<image.sizeInBytes())return 3;
    // Parent owns admission and reads only after this completion frame.
    std::memcpy(memory.data(),image.constBits(),size_t(image.sizeInBytes()));
    QJsonObject reply{{"phase","decoded"},{"width",32},{"height",24},{"stride",128},{"color","sRGB fixture"}};
    if(!config.value("omitSourceToken").toBool())reply["sourceToken"]=token;
    if(!send(reply))return 3;
    char ack[16]{};return std::fgets(ack,sizeof(ack),stdin)?0:3;
}
