#include "io_job_lease.hpp"
#include <QCoreApplication>
#include <QPointer>
#include <QDebug>
#include <QUuid>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);auto& scheduler=glancelot::IoScheduler::instance();
    scheduler.reset();scheduler.setProfile(glancelot::StorageProfile::Ssd);
    auto lease=glancelot::detail::admitIoJob(glancelot::JobPriority::Foreground,3072,"/tmp");
    if(!lease)return 2;std::weak_ptr<glancelot::detail::IoJobLease> weak=lease;
    QObject owner;int released=0;QPointer<QProcess> process=new QProcess;
    process->start("/tmp/glancelot-missing-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto initial=process->state();qInfo()<<"state at retirement"<<initial;
    glancelot::detail::retireIoProcess(process,std::move(lease),&owner,[&]{++released;});
    QTimer poll;QObject::connect(&poll,&QTimer::timeout,&app,[&]{if(!process&&weak.expired()&&released==1)app.quit();});poll.start(10);
    QTimer::singleShot(3000,&app,&QCoreApplication::quit);app.exec();
    const bool ok=initial==QProcess::Starting&&!process&&weak.expired()&&released==1&&scheduler.activeJobCount()==0&&scheduler.admittedBytes()==0;
    qInfo()<<"process deleted"<<!process<<"lease released"<<weak.expired()<<"callbacks"<<released<<"jobs"<<scheduler.activeJobCount()<<"bytes"<<scheduler.admittedBytes();
    if(process)delete process.data();return ok?0:1;
}
