#ifndef CORE_OPERATION_RESULT_H
#define CORE_OPERATION_RESULT_H
#include "domain/Operation.h"
#include "domain/FirmwareInfo.h"
#include "domain/Message.h"
namespace core {
enum class ResultKind { None, DeviceList, FlashId, FlashInfo, ChipInfo, Capability, SecureMode, Sn, PartitionList, FirmwareInfo, Generic };
enum class OperationStatus { Succeeded, Failed, Cancelled };
enum class ErrorCode { None, InvalidInput, DeviceMissing, DeviceAmbiguous, FileAccess, InvalidFirmware, UnsupportedDevice, OperationFailed, Timeout };
struct ResultItem { std::string label; std::string value; };
struct PartitionInfo { std::string name; uint64_t begin; uint64_t count; };
struct FlashInfo {
	uint32_t sizeSectors = 0;
	uint32_t blockSectors = 0;
	uint32_t pageSectors = 0;
	uint8_t manufacturer = 0, eccBits = 0, accessTime = 0, chipSelects = 0;
};
struct OperationResult {
	Operation operation = Operation::Count;
	OperationStatus status = OperationStatus::Failed;
	ErrorCode error = ErrorCode::None;
	ResultKind kind = ResultKind::None;
	std::string diagnostic;
	Message diagnosticMessage;
	// Display-only rows; business decisions use typed fields below.
	std::vector<ResultItem> items;
	FirmwareInfo firmware;
	FlashInfo flash;
	std::vector<uint8_t> bytes;
	std::string serial;
	int secureMode = 0;
	std::vector<PartitionInfo> partitions;
	bool ok() const { return status == OperationStatus::Succeeded; }
};
}
#endif
