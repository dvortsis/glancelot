#pragma once
#include "glancelot/display_manager.hpp"
#include "startup_window_geometry.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace glancelot::monitor_topology {
inline bool strongIdentity(const DisplayInfo& info,const QList<DisplayInfo>& displays) {
    const auto serial=info.serialNumber.trimmed().toCaseFolded();
    if(serial.isEmpty()||QStringList{"0","unknown","none","default","n/a"}.contains(serial))return false;
    if(std::all_of(serial.begin(),serial.end(),[](QChar c){return c=='0'||c==' ';}))return false;
    return std::count_if(displays.begin(),displays.end(),[&](const auto& other){return other.serialNumber.trimmed().toCaseFolded()==serial;})==1;
}
struct Placement {QString displayId;QRect geometry;bool recovered{};};
inline QRect clamp(QRect preferred,QRect area,QSize minimum={360,280}) {
    const std::array<QRect,1> areas{area};
    auto r=startup::recoverStartupWindowGeometry(preferred,areas,0,minimum).geometry;
    if(area.width()<1||area.height()<1)return r;
    const qint64 w=std::max(minimum.width(),std::min(r.width(),area.width()));
    const qint64 h=std::max(minimum.height(),std::min(r.height(),area.height()));
    const auto maxInt=std::numeric_limits<int>::max();
    const qint64 lowX=std::min<qint64>(area.x(),qint64(maxInt)-w+1),lowY=std::min<qint64>(area.y(),qint64(maxInt)-h+1);
    const qint64 highX=std::max(lowX,std::min(qint64(area.x())+area.width()-w,qint64(maxInt)-w+1));
    const qint64 highY=std::max(lowY,std::min(qint64(area.y())+area.height()-h,qint64(maxInt)-h+1));
    const auto x=std::clamp<qint64>(r.x(),lowX,highX),y=std::clamp<qint64>(r.y(),lowY,highY);
    return {QPoint(int(x),int(y)),QPoint(int(x+w-1),int(y+h-1))};
}
inline Placement place(const QString& preferredId,QRect preferred,const QList<DisplayInfo>& displays,
                       const QString& controllerId,bool allowPreferred) {
    const DisplayInfo* target=nullptr;const DisplayInfo* fallback=nullptr;
    if(displays.size()>64)return {{},preferred,true};
    for(const auto& display:displays)if(display.isAvailable&&display.availableGeometry.isValid()) {
        if(!fallback||display.id==controllerId)fallback=&display;
        if(allowPreferred&&display.id==preferredId)target=&display;
    }
    const bool recovered=!target;if(!target)target=fallback;
    if(!target)return {{},preferred,true};
    return {target->id,clamp(preferred,target->availableGeometry),recovered};
}
}
