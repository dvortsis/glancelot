#pragma once
#include <QJsonObject>
namespace glancelot::layout_detail {
// Shared shipped schema/default data; no layout instance or preference access.
inline QJsonObject defaults() {
    QJsonObject result;
    const auto add=[&](const char* id,const char* region,int order,int extent,bool visible) {
        result[id]=QJsonObject{{"region",region},{"order",order},{"extent",extent},{"visible",visible},{"frozen",false}};
    };
    add("tree","left",0,280,true);add("preview","center",0,640,true);add("grid","center",1,640,true);
    add("filmstrip","top",1,112,true);add("topbar","top",0,48,true);add("statusbar","bottom",0,48,true);
    add("metadata","right",0,320,false);
    return result;
}
}
