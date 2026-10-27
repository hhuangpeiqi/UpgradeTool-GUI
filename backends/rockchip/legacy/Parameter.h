#ifndef PARAMETER_HEADER
#define PARAMETER_HEADER

#include "DefineHeader.h"
#include "gpt.h"
namespace platform { class LogService; }

bool ParsePartitionInfo(string &strPartInfo, string &strName, UINT &uiOffset, UINT &uiLen);
bool ParseUuidInfo(string &strUuidInfo, string &strName, string &strUUid);
bool parse_parameter(char *pParameter, PARAM_ITEM_VECTOR &vecItem, CONFIG_ITEM_VECTOR &vecUuidItem);
bool parse_parameter_file(char *pParamFile, PARAM_ITEM_VECTOR &vecItem, CONFIG_ITEM_VECTOR &vecUuidItem, platform::LogService *log = nullptr);
void string_to_uuid(string strUUid, char *uuid);
void gen_rand_uuid(unsigned char *uuid_bin);
bool MakeParamBuffer(char *pParamFile, char *&pParamData, platform::LogService *log = nullptr);
bool is_sparse_image(char *szImage, platform::LogService *log = nullptr);
bool is_ubifs_image(char *szImage, platform::LogService *log = nullptr);

// GPT helpers
void prepare_gpt_backup(u8 *master, u8 *backup);
// pGrowAlign: NULL means "no GROW_ALIGN line in the parameter" (the grow end
// then depends on a cache partition); otherwise *pGrowAlign selects the aligned
// / unaligned grow end.
void create_gpt_buffer(u8 *gpt, PARAM_ITEM_VECTOR &vecParts, CONFIG_ITEM_VECTOR &vecUuid, u64 diskSectors, const bool *pGrowAlign);

// Parameter-file scanners (operate on the whole file content).
int get_parameter_gptflag(char *pParameter);
unsigned int get_parameter_gpt_size(char *pParameter);
bool get_grow_align_flag(char *pParameter, bool &bGrowAlign);
bool load_gpt_buffer(char *pParamFile, u8 *master, u8 *backup, platform::LogService *log = nullptr);
void update_gpt_disksize(u8 *master, u8 *backup, u32 total_sector);
bool get_lba_from_gpt(u8 *master, char *pszName, u64 *lba, u64 *lba_end);
bool get_lba_from_param(u8 *param, char *pszName, u32 *part_offset, u32 *part_size);

#endif
