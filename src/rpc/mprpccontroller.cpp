

#include "mprpccontroller.h"

MprpcController::MprpcController()
{
    failed_ = false;
    errorMsg_ = "";
}

void MprpcController::Reset(){
    failed_ = false;
    errorMsg_ = "";
}

bool MprpcController::Failed() const {
    return failed_;
}

std::string MprpcController::ErrorText() const {
    return errorMsg_;
}

void MprpcController::SetFailed(const std::string& reason) {
    failed_ = true;
    errorMsg_ = reason;
}

// 暂时还没有实现
void MprpcController::StartCancel() {}
bool MprpcController::IsCanceled() const { return false; }
void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback) {}