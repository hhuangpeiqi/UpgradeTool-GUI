#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include "DefineHeader.h"
#include "platform/FileLog.h"

bool parse_config(char *pConfig, CONFIG_ITEM_VECTOR &vecItem);
bool parse_config_file(const char *pConfigFile, CONFIG_ITEM_VECTOR &vecItem, platform::FileLog *log = nullptr);
int find_config_item(CONFIG_ITEM_VECTOR &vecItems, const char *pszName);

#endif
