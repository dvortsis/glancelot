#pragma once
#include "glancelot/io_scheduler.hpp"
#include <QProcess>
#include <QPointer>
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
// release connections belong to the process and survive its UI owner's exit.
inline void retireIoProcess(QProcess* process,std::shared_ptr<IoJobLease> lease,QObject* owner,
                            std::function<void()> released={}) {
    if(!process){lease.reset();if(released)QTimer::singleShot(0,owner,std::move(released));return;}
    process->disconnect(owner);process->setParent(nullptr);
    struct Retirement {std::shared_ptr<IoJobLease> lease;bool disposed{};};
    auto retirement=std::make_shared<Retirement>();retirement->lease=std::move(lease);
    auto dispose=[process,retirement,guard=QPointer<QObject>(owner),released=std::move(released)]() mutable {
        if(std::exchange(retirement->disposed,true))return;
        retirement->lease.reset();process->deleteLater();
        if(guard&&released)QTimer::singleShot(0,guard,std::move(released));
    };
    if(process->state()==QProcess::NotRunning) {
        dispose();return;
    }
    QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,dispose);
    // A cancelled Starting process can fail to exec and never emit finished.
    // Release only once it actually reaches NotRunning, regardless of signal order.
    QObject::connect(process,&QProcess::stateChanged,process,[dispose](QProcess::ProcessState state) mutable {
        if(state==QProcess::NotRunning)dispose();
    });
    QObject::connect(process,&QProcess::errorOccurred,process,[process,dispose](QProcess::ProcessError) mutable {
        if(process->state()==QProcess::NotRunning)dispose();
    });
    process->closeWriteChannel();process->kill();
}
}
