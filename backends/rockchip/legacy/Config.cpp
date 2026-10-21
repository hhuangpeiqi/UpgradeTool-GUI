#include "platform/File.h"
#include "Config.h"
#include <sstream>
#include <mutex>

int find_config_item(CONFIG_ITEM_VECTOR &vecItems, const char *pszName)
{
	unsigned int i;
	for (i = 0; i < vecItems.size(); i++) {
		if (strcasecmp(pszName, vecItems[i].szItemName) == 0)
			return i;
	}
	return -1;
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
			if (strItemName.size() >= sizeof(item.szItemName) || strItemValue.size() >= sizeof(item.szItemValue)) return false;
			strcpy(item.szItemName, strItemName.c_str());
			strcpy(item.szItemValue, strItemValue.c_str());
			vecItem.push_back(item);
		}
	}
	return true;
}

bool parse_config_file(const char *pConfigFile, CONFIG_ITEM_VECTOR &vecItem, platform::FileLog *log)
{
	FILE *file = NULL;
	file = platform::OpenFile(pConfigFile, "rb");
	if (!file) {
		if (log)
			log->Record("%s failed, err=%d, can't open file: %s\r\n", __func__, errno, pConfigFile);
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
		if (log)
			log->Record("%s failed, err=%d, read=%d, total=%d\r\n", __func__, errno, iRead, iFileSize);
		fclose(file);
		delete []pConfigBuf;
		return false;
	}
	fclose(file);
	CONFIG_ITEM_VECTOR vecParsed;
	bool bRet = parse_config(pConfigBuf, vecParsed);
	delete []pConfigBuf;
	if (bRet) {
		vecItem = vecParsed;
	}
	return bRet;
}

