#ifndef MD5CHECKSUM_HEADER
#define MD5CHECKSUM_HEADER

#include "DefineHeader.h"

// MD5 helper.
class CMD5Checksum
{
public:
	CMD5Checksum();
	~CMD5Checksum();

	// Compute the MD5 of `size` bytes read from `file` starting at `offset`.
	string GetMD5(FILE *file, long long offset = 0, long long size = -1);
	// Compute the MD5 of an in-memory buffer.
	string GetMD5(PBYTE buffer, UINT size);

	// Feed data / finish the running digest.
	void Update(PBYTE buffer, UINT size);
	string Final();
	char *FinalChar();

	static void ByteToDWord(UINT *out, PBYTE in, UINT len);
	static void DWordToByte(PBYTE out, UINT *in, UINT len);

protected:
	void _GetMD5(FILE *file, long long size);
	void _GetMD5(PBYTE buffer, UINT size);
	void Transform(PBYTE buffer);

private:
	void Init();
	void Reset();

	UINT m_state[4];
	UINT m_count[2];
	BYTE m_buffer[64];
	char m_digest[33];
};

#endif
