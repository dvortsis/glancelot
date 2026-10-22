#define main candidate_test_main
#include "../tests/selection_export_tests.cpp"
#undef main

int main() {
    Fixture f;
    auto p=f.file("source.png","source");
    std::error_code ec;
    auto snap=snapshotTarget(p,ec);
    ExportItem known{p,TargetKind::File,snap->identity,snap->bytes,snap->modified};
    ExportItem unknown=known; unknown.identity={0,0};
    for (int order=0;order<2;++order) {
        ExportRequest q; q.revalidate=false;
        q.destination=f.root/("mixed-"+std::to_string(order)+".csv");
        q.items=order?std::vector<ExportItem>{unknown,known}:std::vector<ExportItem>{known,unknown};
        auto r=exportSelection(q);
        std::cout<<"mixed-known-unknown order="<<order<<" written="<<r.written<<" skipped="<<r.skipped.size()<<" expected=1/1\n";
        check(r.written==1 && r.skipped.size()==1,"mixed identity duplicates must use lexical path fallback");
    }
    for (int stage=0;stage<2;++stage) {
        ExportRequest q; q.items={known}; q.overwrite=true;
        q.destination=f.file("cancel-"+std::to_string(stage)+".csv","original");
        std::stop_source stop; std::size_t calls=0;
        auto r=exportSelection(q,stop.get_token(),[&](auto done,auto total){
            ++calls;
            if ((stage==0 && done==0)||(stage==1 && done==total)) stop.request_stop();
        });
        std::cout<<"cancel-stage="<<stage<<" status="<<int(r.status)<<" calls="<<calls<<" destination-preserved="<<(readFile(q.destination)=="original")<<" expected=Cancelled/true\n";
        check(r.status==ExportStatus::Cancelled && readFile(q.destination)=="original","last-item/completion cancellation must preserve destination");
        check(calls<=q.items.size(),"progress no more than once per item");
    }
#ifndef _WIN32
    {
        ExportRequest q; q.items={known}; q.destination=f.root/"late-destination.csv";
        bool created=false;
        auto r=exportSelection(q,{},[&](auto,auto){if(!created){write(q.destination,"created-after-precheck");created=true;}});
        std::cout<<"no-overwrite-race status="<<int(r.status)<<" destination-preserved="<<(readFile(q.destination)=="created-after-precheck")<<" expected=true\n";
        check(readFile(q.destination)=="created-after-precheck","overwrite=false must not replace a destination created during export");
    }
    {
        auto root=f.dir("relative-root");
        auto outside=f.dir("outside-root");
        write(outside/"image.png","x");
        fs::create_directory_symlink(outside,root/"alias");
        ExportRequest q; q.destination=f.root/"lexical-relative.csv";
        q.revalidate=false; q.pathPolicy=PathPolicy::RelativeToRoot; q.root=root;
        q.columns={ExportColumn::Path};
        q.items={ExportItem{root/"alias/image.png"}};
        auto r=exportSelection(q);
        std::cout<<"lexical-relative written="<<r.written<<" skipped="<<r.skipped.size()<<" expected=1/0\n";
        check(r.written==1,"relative-root contract compares lexical paths, not symlink targets");
    }
#else
    ExportRequest q; q.items={known};
    q.destination=f.root/fs::path(L"\u6e2c\u8a66-\U0001f5bc.csv");
    try {
        auto r=exportSelection(q);
        std::cout<<"unicode-destination status="<<int(r.status)<<" exists="<<fs::exists(q.destination)<<"\n";
        check(r.status==ExportStatus::Written,"Unicode destination must export successfully");
    } catch(const std::exception& e) {
        std::cout<<"unicode-destination exception="<<e.what()<<"\n";
        check(false,"Unicode destination must not throw on narrow conversion");
    }
#endif
    std::cout<<"Review probe unmet assertions="<<failures<<"\n";
    return failures?1:0;
}
