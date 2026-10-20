#pragma once
#include "glancelot/io_scheduler.hpp"
#include <QProcess>
#include <QTimer>
#include <functional>
#include <memory>
#include <utility>

namespace glancelot::detail {
struct IoJobLease {
    quint64 id{};
    ~IoJobLease(){if(id)IoScheduler::instance().release(id);}
};
inline std::shared_ptr<IoJobLease> admitIoJob(JobPriority priority,qint64 bytes,const QString& path) {
    const auto id=allocateIoJobId();if(!id)return {};
    auto lease=std::make_shared<IoJobLease>();
    if(!IoScheduler::instance().tryAdmit(id,priority,bytes,path))return {};
    lease->id=id;return lease;
}
// Keep the reservation until the killed process has actually stopped. The
// release connection belongs to the process and survives its UI owner's exit.
inline void retireIoProcess(QProcess* process,std::shared_ptr<IoJobLease> lease,QObject* owner,
                            std::function<void()> released={}) {
    if(!process){lease.reset();if(released)QTimer::singleShot(0,owner,std::move(released));return;}
    process->disconnect(owner);process->setParent(nullptr);
    auto dispose=[process,lease=std::move(lease)]() mutable {lease.reset();process->deleteLater();};
    if(process->state()==QProcess::NotRunning) {
        dispose();if(released)QTimer::singleShot(0,owner,std::move(released));return;
    }
    QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,std::move(dispose));
    if(released)QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,std::move(released));
    process->closeWriteChannel();process->kill();
}
}
