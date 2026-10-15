#ifndef TESTS_FAKE_DIAG_SESSION_H
#define TESTS_FAKE_DIAG_SESSION_H
#include "gui/EdlRunner.h"
using DiagExchange = std::function<QByteArray(const gui::EdlEndpoint &, const QByteArray &, const gui::EdlCancel &, QString &, bool &)>;
class FakeDiagSession : public gui::DiagSession {
    gui::EdlEndpoint endpoint;
    DiagExchange callback;
public:
    FakeDiagSession(const gui::EdlEndpoint &e, DiagExchange f) : endpoint(e), callback(std::move(f)) {}
    QByteArray exchange(const QByteArray &request, const gui::EdlCancel &cancel, QString &error, bool &sent) override {
        return callback(endpoint,request,cancel,error,sent);
    }
};
inline std::function<std::unique_ptr<gui::DiagSession>(const gui::EdlEndpoint &, const gui::EdlCancel &, QString &)>
FakeDiagFactory(DiagExchange callback) {
    return [callback](const gui::EdlEndpoint &e,const gui::EdlCancel &,QString &) {
        return std::unique_ptr<gui::DiagSession>(new FakeDiagSession(e,callback));
    };
}
#endif
