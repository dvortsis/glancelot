#include <QCoreApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
QByteArray hashFile(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly),"Cannot hash generated file");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) { const auto bytes=file.read(1024*1024); require(!bytes.isEmpty()||file.atEnd(),"Cannot read generated file"); hash.addData(bytes); }
    return hash.result().toHex();
}
void writeFile(const QString& path,const QByteArray& bytes) {
    QSaveFile file(path); require(file.open(QIODevice::WriteOnly),"Cannot create generated file");
    require(file.write(bytes)==bytes.size()&&file.commit(),"Cannot save generated file");
}
QImage pixels(int width,int height,int seed) {
    QImage image(width,height,QImage::Format_RGB32); require(!image.isNull(),"Fixture allocation failed");
    for(int y=0;y<height;++y) {
        auto* row=reinterpret_cast<QRgb*>(image.scanLine(y));
        for(int x=0;x<width;++x) row[x]=qRgb((x*37+y*17+seed)%256,(x*11+y*29+seed*3)%256,(x+y*7+seed*5)%256);
    }
    image.setColorSpace(QColorSpace::SRgb); return image;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        auto args=app.arguments(); args.removeFirst(); const bool smoke=args.removeAll("--smoke")>0;
        require(args.size()==2,"Usage: glancelot-perf-fixtures manifest.json NEW_EMPTY_OUTPUT [--smoke]");
        QFile source(args[0]); require(source.open(QIODevice::ReadOnly)&&source.size()<=65536,"Invalid fixture manifest");
        const auto manifestBytes=source.readAll(); QJsonParseError parse;
        const auto manifest=QJsonDocument::fromJson(manifestBytes,&parse).object();
        require(parse.error==QJsonParseError::NoError&&manifest.value("schema").toInt()==1,"Unsupported fixture manifest");
        const QString id=manifest.value("id").toString();
        require(!id.isEmpty()&&id.size()<=100,"Invalid fixture id");
        for(auto c:id) require(c.isLetterOrNumber()||c=='-'||c=='_',"Invalid fixture identifier");
        const auto specs=manifest.value("images").toArray(), formats=manifest.value("formats").toArray(), counts=manifest.value("mixed_counts").toArray();
        require(!specs.isEmpty()&&specs.size()<=16&&formats.size()==2&&counts.size()<=8,"Invalid fixture bounds");
        require(formats.contains("png")&&formats.contains("jpeg"),"PNG and JPEG formats are required");
        for(const auto spec:specs) {
            const auto obj=spec.toObject(); const int w=obj.value("width").toInt(),h=obj.value("height").toInt();
            require(w>0&&h>0&&qint64(w)*h<=60000000,"Invalid fixture dimensions");
            const auto name=obj.value("id").toString(); require(!name.isEmpty()&&name.size()<32,"Invalid image id");
            for(auto c:name) require(c.isLetterOrNumber()||c=='-'||c=='_',"Unsafe image id");
        }
        int total{};
        for(const auto count:counts) { const int n=count.toInt(); require(n>0&&n<=100000,"Invalid mixed-folder count"); total+=n; }
        require(total<=150000,"Fixture entry budget exceeded");
        const auto tree=manifest.value("tree").toObject(); const int depth=tree.value("depth").toInt(),branches=tree.value("branching").toInt();
        require(depth>=0&&depth<=8&&branches>=1&&branches<=3,"Invalid tree bounds");
        const QString destination=QFileInfo(args[1]).absoluteFilePath(); QDir output(destination);
        require(!output.isRoot(),"Cannot generate fixtures at a filesystem root");
        require(!output.exists()||output.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot).isEmpty(),"Output must be new or empty; existing files are never overwritten");
        require(QDir().mkpath(destination),"Cannot create output directory");
        const QString fixtureId=id+(smoke?"-smoke":"");
        writeFile(output.filePath(".glancelot-perf-generated"),fixtureId.toUtf8()+"\n");
        QJsonArray files,groups;
        const auto record=[&](const QString& relative,const QByteArray& hash,int width=0,int height=0) {
            require(files.size()<170000,"Generated file count exceeded bound");
            QJsonObject item{{"path",relative},{"sha256",QString::fromLatin1(hash)}};
            if(width) { item["width"]=width; item["height"]=height; }
            files.append(item);
        };
        require(output.mkpath("images"),"Cannot create image directory");
        for(const auto spec:specs) {
            const auto obj=spec.toObject(); const int w=smoke?64:obj.value("width").toInt(),h=smoke?48:obj.value("height").toInt();
            const auto image=pixels(w,h,17);
            for(const auto format:formats) {
                const QString type=format.toString(),relative="images/"+obj.value("id").toString()+"."+(type=="jpeg"?"jpg":"png");
                require(image.save(output.filePath(relative),type.toLatin1().constData(),type=="jpeg"?90:-1),"Cannot encode fixture");
                record(relative,hashFile(output.filePath(relative)),w,h);
            }
        }
        const auto thumbnail=pixels(64,48,29);
        const QString tile="tile.png"; require(thumbnail.save(output.filePath(tile),"PNG"),"Cannot encode tile");
        const auto tileHash=hashFile(output.filePath(tile)); record(tile,tileHash,64,48);
        const QByteArray note="Glancelot deterministic unsupported file fixture v1\n";
        const auto noteHash=QCryptographicHash::hash(note,QCryptographicHash::Sha256).toHex();
        for(const auto count:counts) {
            const int requested=count.toInt(),actual=smoke?qMin(100,requested):requested;
            const QString folder=QString("mixed-%1").arg(requested); require(output.mkpath(folder),"Cannot create mixed folder");
            for(int i=0;i<actual;++i) {
                const bool photo=i%10==0;
                const QString relative=folder+QString(photo?"/image-%1.png":"/note-%1.txt").arg(i,6,10,QLatin1Char('0'));
                if(photo) require(QFile::copy(output.filePath(tile),output.filePath(relative)),"Cannot copy generated tile");
                else writeFile(output.filePath(relative),note);
                record(relative,photo?tileHash:noteHash,photo?64:0,photo?48:0);
            }
            groups.append(QJsonObject{{"path",folder},{"requested_entries",requested},{"actual_entries",actual}});
        }
        std::function<void(QString,int)> nested=[&](QString folder,int remaining) {
            require(output.mkpath(folder),"Cannot create nested folder");
            const auto relative=folder+"/image.png";
            require(QFile::copy(output.filePath(tile),output.filePath(relative)),"Cannot copy tree tile"); record(relative,tileHash,64,48);
            if(remaining>0) for(int n=0;n<branches;++n) nested(folder+QString("/level-%1").arg(n),remaining-1);
        };
        nested("nested",smoke?qMin(2,depth):depth);
        const QJsonObject index{{"schema",1},{"id",fixtureId},{"profile",smoke?"smoke":"full"},{"qt",qVersion()},
            {"manifest_sha256",QString::fromLatin1(QCryptographicHash::hash(manifestBytes,QCryptographicHash::Sha256).toHex())},
            {"mixed_folders",groups},{"files",files}};
        writeFile(output.filePath("fixture-index.json"),QJsonDocument(index).toJson());
        std::printf("Generated %s: %lld files; Qt %s; profile %s\n",qPrintable(fixtureId),static_cast<long long>(files.size()),qVersion(),smoke?"smoke":"full");
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"Fixture generation failed: %s\n",error.what()); return 1; }
}
