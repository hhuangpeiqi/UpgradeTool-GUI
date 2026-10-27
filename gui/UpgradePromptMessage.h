#pragma once
#include "domain/UpgradeObserver.h"
#include <QCoreApplication>
namespace gui {
inline core::Message UpgradePromptMessage(int prompt, bool &failed)
{
	failed = false;
	switch (prompt) {
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_BOOT_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Downloading Boot"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_BOOT_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Download Boot failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_BOOT_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Download Boot succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_IDB_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Downloading IDB"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_IDB_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Download IDB failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_IDB_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Download IDB succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_FIRMWARE_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Downloading firmware"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_FIRMWARE_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Download firmware failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_FIRMWARE_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Download firmware succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_TEST_DEVICE_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Testing device"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_TEST_DEVICE_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Test device failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_TEST_DEVICE_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Test device succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_RESET_DEVICE_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Resetting device"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_RESET_DEVICE_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Reset device failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_RESET_DEVICE_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Reset device succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MSC_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Waiting for MSC mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MSC_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Waiting for MSC mode failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MSC_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Entered MSC mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_LOADER_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Waiting for Loader mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_LOADER_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Waiting for Loader mode failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_LOADER_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Entered Loader mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Waiting for Maskrom mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Waiting for Maskrom mode failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Entered Maskrom mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_IDB_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Erasing IDB"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_IDB_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Erase IDB failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_IDB_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Erase IDB succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_SWITCH_MSC_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Switching to MSC mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_SWITCH_MSC_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Switch to MSC mode failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_SWITCH_MSC_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Switched to MSC mode"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_CHECK_CHIP_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Checking chip"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_CHECK_CHIP_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Check chip failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_CHECK_CHIP_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Check chip succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_PREPARE_IDB_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Preparing IDB"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_PREPARE_IDB_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Prepare IDB failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_PREPARE_IDB_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Prepare IDB done"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_FLASH_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Erasing Flash"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_FLASH_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Erase Flash failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_FLASH_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Erase Flash succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_BLOCK_STATE_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Reading block state"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_BLOCK_STATE_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Read block state failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_BLOCK_STATE_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Read block state succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_FLASH_INFO_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Reading Flash info"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_FLASH_INFO_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Read Flash info failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_FLASH_INFO_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Read Flash info succeeded"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_LOWER_FORMAT_START): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Low-level formatting"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_LOWER_FORMAT_FAIL): failed = true; return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Low-level format failed"), {}};
	case static_cast<int>(core::UpgradePrompt::PROMPT_LOWER_FORMAT_SUCCESS): return core::Message{"QObject", QT_TRANSLATE_NOOP("QObject", "Low-level format succeeded"), {}};
	default: return {};
	}
}
}
