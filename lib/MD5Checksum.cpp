/*
 * MD5 (RFC 1321) implementation.
 */
#include "MD5Checksum.h"

#include <vector>

#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

static inline UINT F(UINT x, UINT y, UINT z) { return (x & y) | (~x & z); }
static inline UINT G(UINT x, UINT y, UINT z) { return (x & z) | (y & ~z); }
static inline UINT H(UINT x, UINT y, UINT z) { return x ^ y ^ z; }
static inline UINT I(UINT x, UINT y, UINT z) { return y ^ (x | ~z); }
static inline UINT ROTATE_LEFT(UINT x, int n) { return (x << n) | (x >> (32 - n)); }

static inline void FF(UINT &a, UINT b, UINT c, UINT d, UINT x, UINT s, UINT ac)
{
	a += F(b, c, d) + x + ac;
	a = ROTATE_LEFT(a, s);
	a += b;
}
static inline void GG(UINT &a, UINT b, UINT c, UINT d, UINT x, UINT s, UINT ac)
{
	a += G(b, c, d) + x + ac;
	a = ROTATE_LEFT(a, s);
	a += b;
}
static inline void HH(UINT &a, UINT b, UINT c, UINT d, UINT x, UINT s, UINT ac)
{
	a += H(b, c, d) + x + ac;
	a = ROTATE_LEFT(a, s);
	a += b;
}
static inline void II(UINT &a, UINT b, UINT c, UINT d, UINT x, UINT s, UINT ac)
{
	a += I(b, c, d) + x + ac;
	a = ROTATE_LEFT(a, s);
	a += b;
}

static const BYTE PADDING[64] = { 0x80 };

CMD5Checksum::CMD5Checksum()
{
	Reset();
}

CMD5Checksum::~CMD5Checksum()
{
}

void CMD5Checksum::Reset()
{
	m_state[0] = 0x67452301;
	m_state[1] = 0xefcdab89;
	m_state[2] = 0x98badcfe;
	m_state[3] = 0x10325476;
	m_count[0] = m_count[1] = 0;
	memset(m_buffer, 0, sizeof(m_buffer));
	memset(m_digest, 0, sizeof(m_digest));
}

void CMD5Checksum::Init()
{
	Reset();
}

void CMD5Checksum::ByteToDWord(UINT *out, PBYTE in, UINT len)
{
	UINT i, j;
	for (i = 0, j = 0; j < len; i++, j += 4)
		out[i] = ((UINT)in[j]) | (((UINT)in[j + 1]) << 8) |
		         (((UINT)in[j + 2]) << 16) | (((UINT)in[j + 3]) << 24);
}

void CMD5Checksum::DWordToByte(PBYTE out, UINT *in, UINT len)
{
	UINT i, j;
	for (i = 0, j = 0; j < len; i++, j += 4) {
		out[j] = (BYTE)(in[i] & 0xff);
		out[j + 1] = (BYTE)((in[i] >> 8) & 0xff);
		out[j + 2] = (BYTE)((in[i] >> 16) & 0xff);
		out[j + 3] = (BYTE)((in[i] >> 24) & 0xff);
	}
}

void CMD5Checksum::Transform(PBYTE buffer)
{
	UINT a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
	UINT x[16];

	ByteToDWord(x, buffer, 64);

	FF(a, b, c, d, x[0], S11, 0xd76aa478);  FF(d, a, b, c, x[1], S12, 0xe8c7b756);
	FF(c, d, a, b, x[2], S13, 0x242070db);  FF(b, c, d, a, x[3], S14, 0xc1bdceee);
	FF(a, b, c, d, x[4], S11, 0xf57c0faf);  FF(d, a, b, c, x[5], S12, 0x4787c62a);
	FF(c, d, a, b, x[6], S13, 0xa8304613);  FF(b, c, d, a, x[7], S14, 0xfd469501);
	FF(a, b, c, d, x[8], S11, 0x698098d8);  FF(d, a, b, c, x[9], S12, 0x8b44f7af);
	FF(c, d, a, b, x[10], S13, 0xffff5bb1); FF(b, c, d, a, x[11], S14, 0x895cd7be);
	FF(a, b, c, d, x[12], S11, 0x6b901122); FF(d, a, b, c, x[13], S12, 0xfd987193);
	FF(c, d, a, b, x[14], S13, 0xa679438e); FF(b, c, d, a, x[15], S14, 0x49b40821);

	GG(a, b, c, d, x[1], S21, 0xf61e2562);  GG(d, a, b, c, x[6], S22, 0xc040b340);
	GG(c, d, a, b, x[11], S23, 0x265e5a51); GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);
	GG(a, b, c, d, x[5], S21, 0xd62f105d);  GG(d, a, b, c, x[10], S22, 0x02441453);
	GG(c, d, a, b, x[15], S23, 0xd8a1e681); GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);
	GG(a, b, c, d, x[9], S21, 0x21e1cde6);  GG(d, a, b, c, x[14], S22, 0xc33707d6);
	GG(c, d, a, b, x[3], S23, 0xf4d50d87);  GG(b, c, d, a, x[8], S24, 0x455a14ed);
	GG(a, b, c, d, x[13], S21, 0xa9e3e905); GG(d, a, b, c, x[2], S22, 0xfcefa3f8);
	GG(c, d, a, b, x[7], S23, 0x676f02d9);  GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

	HH(a, b, c, d, x[5], S31, 0xfffa3942);  HH(d, a, b, c, x[8], S32, 0x8771f681);
	HH(c, d, a, b, x[11], S33, 0x6d9d6122); HH(b, c, d, a, x[14], S34, 0xfde5380c);
	HH(a, b, c, d, x[1], S31, 0xa4beea44);  HH(d, a, b, c, x[4], S32, 0x4bdecfa9);
	HH(c, d, a, b, x[7], S33, 0xf6bb4b60);  HH(b, c, d, a, x[10], S34, 0xbebfbc70);
	HH(a, b, c, d, x[13], S31, 0x289b7ec6); HH(d, a, b, c, x[0], S32, 0xeaa127fa);
	HH(c, d, a, b, x[3], S33, 0xd4ef3085);  HH(b, c, d, a, x[6], S34, 0x04881d05);
	HH(a, b, c, d, x[9], S31, 0xd9d4d039);  HH(d, a, b, c, x[12], S32, 0xe6db99e5);
	HH(c, d, a, b, x[15], S33, 0x1fa27cf8); HH(b, c, d, a, x[2], S34, 0xc4ac5665);

	II(a, b, c, d, x[0], S41, 0xf4292244);  II(d, a, b, c, x[7], S42, 0x432aff97);
	II(c, d, a, b, x[14], S43, 0xab9423a7); II(b, c, d, a, x[5], S44, 0xfc93a039);
	II(a, b, c, d, x[12], S41, 0x655b59c3); II(d, a, b, c, x[3], S42, 0x8f0ccc92);
	II(c, d, a, b, x[10], S43, 0xffeff47d); II(b, c, d, a, x[1], S44, 0x85845dd1);
	II(a, b, c, d, x[8], S41, 0x6fa87e4f);  II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
	II(c, d, a, b, x[6], S43, 0xa3014314);  II(b, c, d, a, x[13], S44, 0x4e0811a1);
	II(a, b, c, d, x[4], S41, 0xf7537e82);  II(d, a, b, c, x[11], S42, 0xbd3af235);
	II(c, d, a, b, x[2], S43, 0x2ad7d2bb);  II(b, c, d, a, x[9], S44, 0xeb86d391);

	m_state[0] += a;
	m_state[1] += b;
	m_state[2] += c;
	m_state[3] += d;
}

void CMD5Checksum::Update(PBYTE input, UINT inputLen)
{
	UINT i, index, partLen;

	index = (m_count[0] >> 3) & 0x3F;

	if ((m_count[0] += (inputLen << 3)) < (inputLen << 3))
		m_count[1]++;
	m_count[1] += (inputLen >> 29);

	partLen = 64 - index;

	if (inputLen >= partLen) {
		memcpy(&m_buffer[index], input, partLen);
		Transform(m_buffer);
		for (i = partLen; i + 63 < inputLen; i += 64)
			Transform(&input[i]);
		index = 0;
	} else {
		i = 0;
	}

	memcpy(&m_buffer[index], &input[i], inputLen - i);
}

string CMD5Checksum::Final()
{
	BYTE bits[8];
	UINT index, padLen;

	DWordToByte(bits, m_count, 8);
	index = (m_count[0] >> 3) & 0x3f;
	padLen = (index < 56) ? (56 - index) : (120 - index);
	Update((PBYTE)PADDING, padLen);
	Update(bits, 8);
	DWordToByte((PBYTE)m_digest, m_state, 16);

	char hex[33];
	for (int i = 0; i < 16; i++)
		sprintf(hex + i * 2, "%02x", (BYTE)m_digest[i]);
	hex[32] = 0;
	return string(hex);
}

char *CMD5Checksum::FinalChar()
{
	Final();
	return m_digest;
}

void CMD5Checksum::_GetMD5(PBYTE buffer, UINT size)
{
	Update(buffer, size);
}

void CMD5Checksum::_GetMD5(FILE *file, long long size)
{
	std::vector<BYTE> buf(64 * 1024);
	long long remain = size;
	while (remain > 0) {
		UINT want = (remain > (long long)buf.size()) ? (UINT)buf.size() : (UINT)remain;
		UINT got = fread(buf.data(), 1, want, file);
		if (got == 0)
			break;
		Update(buf.data(), got);
		remain -= got;
	}
}

string CMD5Checksum::GetMD5(PBYTE buffer, UINT size)
{
	Reset();
	_GetMD5(buffer, size);
	return Final();
}

string CMD5Checksum::GetMD5(FILE *file, long long offset, long long size)
{
	Reset();
	if (!file)
		return "";
	if (size < 0) {
		long long cur = ftello(file);
		fseeko(file, 0, SEEK_END);
		size = ftello(file) - offset;
		fseeko(file, cur, SEEK_SET);
	}
	if (offset)
		fseeko(file, offset, SEEK_SET);
	_GetMD5(file, size);
	return Final();
}
