#ifndef CRC_HEADER
#define CRC_HEADER

#include "DefineHeader.h"

UINT CRC_32(unsigned char *pData, UINT ulSize);
unsigned short CRC_16(unsigned char *aData, UINT aSize);
unsigned int crc32_le(unsigned int crc, unsigned char *p, unsigned int len);
void CRCBuildTable16(unsigned short aPoly, unsigned short *crcTable);
unsigned short CRC_Calculate(unsigned short crc, unsigned char ch);
unsigned short CRC_CCITT(unsigned char *p, UINT CalculateNumber);
void P_RC4(unsigned char *buf, unsigned int len);
void generate_gf();
void gen_poly();
void bch_encode(unsigned char *encode_in, unsigned char *encode_out);

extern UINT gTable_Crc32[256];
extern unsigned int crc32table_le[];
extern UINT alpha_to[], index_of[], gg[], p[];
extern UINT s[], rr, ggx1, ggx2, ggx3, ggx4;

#endif
