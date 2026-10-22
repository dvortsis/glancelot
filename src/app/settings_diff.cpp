#include "settings_diff.hpp"
#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace glancelot::recovery::diff {
namespace {
class Builder {
    Options options;
    const QList<QStringList>& owned;
    Result result;
    bool failed{};
    bool stop(Code code,bool truncated=false) {
        failed=true;result.status={code,{},options.generation};result.truncated=truncated;return false;
    }
    bool cancelled() {if(options.cancellation.stop_requested()){stop(Code::Cancelled);return true;}return false;}
    bool charge(qint64& total,qint64 bytes) {
        if(bytes>MaxPreviewBytes-total)return stop(Code::LimitExceeded,true);
        total+=bytes;return true;
    }
    // A UTF16 unit needs at most six JSON escape bytes. Include QString/QJson
    // and list/node overhead too; charge before allocating row serialization.
    bool textCost(const QString& text,qint64& total) {return charge(total,64+6*qint64(text.size()));}
    bool valueCost(const QJsonValue& value,int depth,qint64& total) {
        if(cancelled())return false;
        if(depth>MaxDiffDepth)return stop(Code::LimitExceeded,true);
        if(!charge(total,64))return false;
        if(value.isString())return textCost(value.toString(),total);
        if(value.isArray()) {
            for(const auto& child:value.toArray())if(!valueCost(child,depth+1,total))return false;
        }else if(value.isObject()) {
            const auto object=value.toObject();
            for(auto it=object.begin();it!=object.end();++it)
                if(!textCost(it.key(),total)||!valueCost(it.value(),depth+1,total))return false;
        }
        return true;
    }
    bool exact(const QStringList& path)const {return owned.contains(path);}
    bool ancestor(const QStringList& path)const {
        for(const auto& field:owned)if(field.size()>path.size()&&std::equal(path.begin(),path.end(),field.begin()))return true;
        return false;
    }
    bool row(const QStringList& path,const QJsonValue& before,const QJsonValue& after,Treatment treatment) {
        if(before==after)return true;
        if(result.rows.size()>=MaxDiffRows)return stop(Code::LimitExceeded,true);
        qint64 cost=512;for(const auto& part:path)if(!textCost(part,cost))return false;
        if(!valueCost(before,path.size(),cost)||!valueCost(after,path.size(),cost))return false;
        if(!charge(result.accountedBytes,cost))return false;
        DiffRow value;value.path=path;value.treatment=treatment;
        value.hadBefore=!before.isUndefined();value.hasAfter=!after.isUndefined();value.before=before;value.after=after;
        value.change=!value.hadBefore?Change::Added:!value.hasAfter?Change::Removed:Change::Changed;
        result.rows.append(std::move(value));return true;
    }
    bool walk(const QJsonValue& current,const QJsonValue& source,const QJsonValue& replacement,QStringList& path) {
        if(cancelled())return false;
        if(path.size()>MaxDiffDepth)return stop(Code::LimitExceeded,true);
        if(exact(path))return row(path,current,replacement,Treatment::Apply);
        const bool hasOwnedChildren=ancestor(path);
        const bool pairObjects=(current.isObject()||current.isUndefined())&&(source.isObject()||source.isUndefined());
        if(!hasOwnedChildren&&!pairObjects)return row(path,current,source,Treatment::Preserve);
        if(!hasOwnedChildren&&current==source)return true;
        if(hasOwnedChildren&&(!current.isObject()&&!current.isUndefined()))return stop(Code::Corrupt);
        if(hasOwnedChildren&&(!source.isObject()&&!source.isUndefined()))return stop(Code::Corrupt);
        const auto a=current.toObject(),b=source.toObject(),c=replacement.toObject();
        // Bounded inputs cap this union; report construction remains separately
        // bounded. Sorting gives stable previews independent of insertion order.
        QSet<QString> names;for(auto it=a.begin();it!=a.end();++it)names.insert(it.key());
        for(auto it=b.begin();it!=b.end();++it)names.insert(it.key());
        if(hasOwnedChildren)for(auto it=c.begin();it!=c.end();++it)names.insert(it.key());
        auto sorted=names.values();std::sort(sorted.begin(),sorted.end());
        for(const auto& key:sorted) {
            path.append(key);
            if(!walk(a.value(key),b.value(key),c.value(key),path))return false;
            path.removeLast();
        }
        // Added/removed empty objects still need a row: they are not absence.
        if(sorted.isEmpty()&&!hasOwnedChildren)return row(path,current,source,Treatment::Preserve);
        return true;
    }
public:
    Builder(Options value,const QList<QStringList>& paths):options(value),owned(paths){
        result.status={Code::Ok,{},value.generation};
        result.accountedBytes=8192; // revisions, status, scope and JSON report envelope
    }
    Result run(const QJsonObject& current,const QJsonObject& source,const QJsonObject& replacement) {
        // Inspect unchanged subtrees too: equality cannot hide an over-deep input.
        // Input validation is bounded independently from aggregate output cost.
        for(const auto& input:{current,source,replacement}) {
            qint64 bytes=0;if(!valueCost(input,0,bytes))return std::move(result);
        }
        QStringList path;walk(current,source,replacement,path);
        if(!failed)result.status.code=Code::Ok;
        return std::move(result);
    }
};
}
Result build(const QJsonObject& current,const QJsonObject& source,const QJsonObject& replacement,
             const QList<QStringList>& paths,Options options) {
    return Builder(options,paths).run(current,source,replacement);
}
}
