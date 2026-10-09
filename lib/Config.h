#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include "DefineHeader.h"
#include "RKLog.h"

// Global configuration/parameter stores (g_ConfigItemVec, g_ParamItemVec).
extern CONFIG_ITEM_VECTOR g_ConfigItemVec;
extern PARAM_ITEM_VECTOR g_ParamItemVec;

bool parse_config(char *pConfig, CONFIG_ITEM_VECTOR &vecItem);
bool parse_config_file(const char *pConfigFile, CONFIG_ITEM_VECTOR &vecItem);
int find_config_item(CONFIG_ITEM_VECTOR &vecItems, const char *pszName);
int find_config_item(const char *pszName);

// Thread-safe copy of one stored value. Returns false when the key is absent.
// This is the accessor runtime reload uses to avoid racing worker-thread reads.
bool GetConfigString(const char *pszName, std::string &strValue);

// Typed getters for the documented config.ini keys (see Config.cpp). Each
// returns false and leaves the output untouched when the key is absent.
bool GetParityFlag(bool &bValue);
bool GetReadCheckFlag(bool &bValue);
bool GetNorSingleIDBFlag(bool &bValue);
bool GetLogFlag(bool &bValue);
bool GetLogDir(string &strDir);
UINT GetConsoleBufferFlag();
bool GetForceDataBand(UINT &uiBand);
unsigned int GetDeviceIDFromConfig();

#endif
