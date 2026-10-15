#ifndef APPLICATION_INSPECTION_H
#define APPLICATION_INSPECTION_H
#include "domain/Operation.h"
namespace core {
struct InspectionRequest {
    OperationRequest request;
    bool directory=false;
    int context=0;
    uint64_t generation=0;
    Cancellation cancelled=std::make_shared<std::stop_source>();
};
}
#endif
