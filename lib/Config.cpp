#include "Config.h"
#include <sstream>
#include <mutex>

CONFIG_ITEM_VECTOR g_ConfigItemVec;
PARAM_ITEM_VECTOR g_ParamItemVec;

extern CRKLog *g_pLogObject;

// The GUI reloads config.ini at runtime while the worker thread reads the
// global store through the helpers below, so every access to g_ConfigItemVec
// goes through this lock.
static std::mutex g_ConfigMutex;

int find_config_item(CONFIG_ITEM_VECTOR &vecItems, const char *pszName)
{
	unsigned int i;
	for (i = 0; i < vecItems.size(); i++) {
		if (strcasecmp(pszName, vecItems[i].szItemName) == 0)
			return i;
	}
	return -1;
}

int find_config_item(const char *pszName)
{
	std::lock_guard<std::mutex> lock(g_ConfigMutex);
	return find_config_item(g_ConfigItemVec, pszName);
}

bool GetConfigString(const char *pszName, std::string &strValue)
{
	std::lock_guard<std::mutex> lock(g_ConfigMutex);
	int pos = find_config_item(g_ConfigItemVec, pszName);
	if (pos < 0)
		return false;
	strValue = g_ConfigItemVec[pos].szItemValue;
	return true;
}

bool parse_config(char *pConfig, CONFIG_ITEM_VECTOR &vecItem)
{
	stringstream configStream(pConfig);
	string strLine, strItemName, strItemValue;
	string::size_type line_size, pos;
	STRUCT_CONFIG_ITEM item;
	vecItem.clear();
	while (!configStream.eof()) {
		getline(configStream, strLine);
		line_size = strLine.size();
		if (line_size == 0)
			continue;
		if (strLine[line_size - 1] == '\r')
			strLine = strLine.substr(0, line_size - 1);
		strLine.erase(0, strLine.find_first_not_of(" "));
		strLine.erase(strLine.find_last_not_of(" ") + 1);
		if (strLine.size() == 0)
			continue;
		if (strLine[0] == '#')
			continue;
		pos = strLine.find("=");
		if (pos == string::npos)
			continue;
		strItemName = strLine.substr(0, pos);
		strItemValue = strLine.substr(pos + 1);
		strItemName.erase(0, strItemName.find_first_not_of(" "));
		strItemName.erase(strItemName.find_last_not_of(" ") + 1);
		strItemValue.erase(0, strItemValue.find_first_not_of(" "));
		strItemValue.erase(strItemValue.find_last_not_of(" ") + 1);
		if ((strItemName.size() > 0) && (strItemValue.size() > 0)) {
			strcpy(item.szItemName, strItemName.c_str());
			strcpy(item.szItemValue, strItemValue.c_str());
			vecItem.push_back(item);
		}
	}
	return true;
}

bool parse_config_file(const char *pConfigFile, CONFIG_ITEM_VECTOR &vecItem)
{
	FILE *file = NULL;
	file = fopen(pConfigFile, "rb");
	if (!file) {
		if (g_pLogObject)
			g_pLogObject->Record("%s failed, err=%d, can't open file: %s\r\n", __func__, errno, pConfigFile);
		return false;
	}
	int iFileSize;
	fseek(file, 0, SEEK_END);
	iFileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *pConfigBuf = new char[iFileSize + 1];
	if (!pConfigBuf) {
		fclose(file);
		return false;
	}
	memset(pConfigBuf, 0, iFileSize + 1);
	int iRead = fread(pConfigBuf, 1, iFileSize, file);
	if (iRead != iFileSize) {
		if (g_pLogObject)
			g_pLogObject->Record("%s failed, err=%d, read=%d, total=%d\r\n", __func__, errno, iRead, iFileSize);
		fclose(file);
		delete []pConfigBuf;
		return false;
	}
	fclose(file);
	CONFIG_ITEM_VECTOR vecParsed;
	bool bRet = parse_config(pConfigBuf, vecParsed);
	delete []pConfigBuf;
	if (bRet) {
		std::lock_guard<std::mutex> lock(g_ConfigMutex);
		vecItem = vecParsed;
	}
	return bRet;
}

// ---------------------------------------------------------------------------
// Typed getters for the documented config.ini keys. Each returns false and
// leaves the output untouched when the key is absent. They lock internally so
// a concurrent runtime reload cannot race a worker-thread lookup.
// ---------------------------------------------------------------------------
static bool get_config_bool(const char *pszName, bool &bValue)
{
	std::string strValue;
	if (!GetConfigString(pszName, strValue))
		return false;
	bValue = (strcasecmp(strValue.c_str(), "true") == 0) ||
	         (strcasecmp(strValue.c_str(), "yes") == 0) ||
	         (atoi(strValue.c_str()) != 0);
	return true;
}

bool GetParityFlag(bool &bValue)      { return get_config_bool("lba_parity", bValue); }
bool GetReadCheckFlag(bool &bValue)   { return get_config_bool("rb_check_off", bValue); }
bool GetNorSingleIDBFlag(bool &bValue){ return get_config_bool("nor_single_idb", bValue); }
bool GetLogFlag(bool &bValue)         { return get_config_bool("log_off", bValue); }

bool GetLogDir(string &strDir)
{
	return GetConfigString("log_dir", strDir);
}

UINT GetConsoleBufferFlag()
{
	bool bOff = false;
	if (get_config_bool("stdout_buffer_off", bOff))
		return bOff ? 1 : 0;
	return 0;
}

bool GetForceDataBand(UINT &uiBand)
{
	std::string strValue;
	if (!GetConfigString("force_data_band", strValue))
		return false;
	uiBand = strtoul(strValue.c_str(), NULL, 0);
	return true;
}

unsigned int GetDeviceIDFromConfig()
{
	std::string strValue;
	if (!GetConfigString("device", strValue))
		return 0;
	return (unsigned int)strtoul(strValue.c_str(), NULL, 0);
}
