//=====================================================================
//
// isecure.c - secure hash encrypt
//
// NOTE:
// for more information, please see the readme file.
//
// Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.
// Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
//
//=====================================================================
#include <stdlib.h>
#include <assert.h>

#include "isecure.h"


//=====================================================================
// INLINE
//=====================================================================
#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE 
#endif
#endif

#ifndef inline
#define inline INLINE
#endif


/* encode 8 bits unsigned int */
static inline char *is_encode8u(char *p, IUINT8 c) {
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *is_decode8u(const char *p, IUINT8 *c) {
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *is_encode16u_lsb(char *p, IUINT16 w) {
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char *is_decode16u_lsb(const char *p, IUINT16 *w) {
#if IWORDS_BIG_ENDIAN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 16 bits unsigned int (msb) */
static inline char *is_encode16u_msb(char *p, IUINT16 w) {
#if IWORDS_BIG_ENDIAN
	memcpy(p, &w, 2);
#else
	*(unsigned char*)(p + 0) = (w >> 8);
	*(unsigned char*)(p + 1) = (w & 255);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (msb) */
static inline const char *is_decode16u_msb(const char *p, IUINT16 *w) {
#if IWORDS_BIG_ENDIAN
	memcpy(w, p, 2);
#else
	*w = *(const unsigned char*)(p + 0);
	*w = *(const unsigned char*)(p + 1) + (*w << 8);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *is_encode32u_lsb(char *p, IUINT32 l) {
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *is_decode32u_lsb(const char *p, IUINT32 *l) {
#if IWORDS_BIG_ENDIAN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

/* encode 32 bits unsigned int (msb) */
static inline char *is_encode32u_msb(char *p, IUINT32 l) {
	*(unsigned char*)(p + 0) = (unsigned char)((l >> 24) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >>  0) & 0xff);
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (msb) */
static inline const char *is_decode32u_msb(const char *p, IUINT32 *l) {
	*l = *(const unsigned char*)(p + 0);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 3) + (*l << 8);
	p += 4;
	return p;
}

/* bits rotation */
static inline IUINT32 is_rotl32(IUINT32 x, int n) {
	return (x << n) | (x >> (32 - n));
}

/* pack bytes into uint32_t */
static inline IUINT32 is_pack4(const unsigned char *a) {
	IUINT32 res = 0;
	res |= (IUINT32)a[0] << 0 * 8;
	res |= (IUINT32)a[1] << 1 * 8;
	res |= (IUINT32)a[2] << 2 * 8;
	res |= (IUINT32)a[3] << 3 * 8;
	return res;
}

/* unpack bytes from uint32_t */
static inline void is_unpack4(IUINT32 src, unsigned char *dst) {
	dst[0] = (src >> 0 * 8) & 0xff;
	dst[1] = (src >> 1 * 8) & 0xff;
	dst[2] = (src >> 2 * 8) & 0xff;
	dst[3] = (src >> 3 * 8) & 0xff;
}


//=====================================================================
// MD5
//=====================================================================
static unsigned char HASH_MD5_PADDING[64] = {
	0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* HASH_MD5_F, HASH_MD5_G and HASH_MD5_H are basic MD5 functions: selection, majority, parity */
#define HASH_MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define HASH_MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define HASH_MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define HASH_MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

/* HASH_ROTATE rotates x left n bits */
#ifndef HASH_ROTATE
#define HASH_ROTATE(x, n) (((x) << (n)) | ((x) >> (32-(n))))
#endif

/* HASH_MD5_FF, HASH_MD5_GG, HASH_MD5_HH, and HASH_MD5_II transformations for */
/* rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation */
#define HASH_MD5_FF(a, b, c, d, x, s, ac) { \
		(a) += HASH_MD5_F ((b), (c), (d)) + (x) + (IUINT32)(ac); \
		(a) = HASH_ROTATE ((a), (s)); (a) += (b); }

#define HASH_MD5_GG(a, b, c, d, x, s, ac) { \
		(a) += HASH_MD5_G ((b), (c), (d)) + (x) + (IUINT32)(ac); \
		(a) = HASH_ROTATE ((a), (s)); (a) += (b); }

#define HASH_MD5_HH(a, b, c, d, x, s, ac) { \
		(a) += HASH_MD5_H ((b), (c), (d)) + (x) + (IUINT32)(ac); \
		(a) = HASH_ROTATE ((a), (s)); (a) += (b); }

#define HASH_MD5_II(a, b, c, d, x, s, ac) { \
		(a) += HASH_MD5_I ((b), (c), (d)) + (x) + (IUINT32)(ac); \
		(a) = HASH_ROTATE ((a), (s)); (a) += (b); }

/* Constants for transformation */
#define HASH_MD5_S11 7  /* Round 1 */
#define HASH_MD5_S12 12
#define HASH_MD5_S13 17
#define HASH_MD5_S14 22
#define HASH_MD5_S21 5  /* Round 2 */
#define HASH_MD5_S22 9
#define HASH_MD5_S23 14
#define HASH_MD5_S24 20
#define HASH_MD5_S31 4  /* Round 3 */
#define HASH_MD5_S32 11
#define HASH_MD5_S33 16
#define HASH_MD5_S34 23
#define HASH_MD5_S41 6  /* Round 4 */
#define HASH_MD5_S42 10
#define HASH_MD5_S43 15
#define HASH_MD5_S44 21

/* Basic MD5 step. HASH_MD5_Transform buf based on in */
static void HASH_MD5_Transform(IUINT32 *buf, const IUINT32 *in)
{
	IUINT32 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

	/* Round 1 */
	HASH_MD5_FF ( a, b, c, d, in[ 0], HASH_MD5_S11, (IUINT32) 3614090360u); /* 1 */
	HASH_MD5_FF ( d, a, b, c, in[ 1], HASH_MD5_S12, (IUINT32) 3905402710u); /* 2 */
	HASH_MD5_FF ( c, d, a, b, in[ 2], HASH_MD5_S13, (IUINT32)  606105819u); /* 3 */
	HASH_MD5_FF ( b, c, d, a, in[ 3], HASH_MD5_S14, (IUINT32) 3250441966u); /* 4 */
	HASH_MD5_FF ( a, b, c, d, in[ 4], HASH_MD5_S11, (IUINT32) 4118548399u); /* 5 */
	HASH_MD5_FF ( d, a, b, c, in[ 5], HASH_MD5_S12, (IUINT32) 1200080426u); /* 6 */
	HASH_MD5_FF ( c, d, a, b, in[ 6], HASH_MD5_S13, (IUINT32) 2821735955u); /* 7 */
	HASH_MD5_FF ( b, c, d, a, in[ 7], HASH_MD5_S14, (IUINT32) 4249261313u); /* 8 */
	HASH_MD5_FF ( a, b, c, d, in[ 8], HASH_MD5_S11, (IUINT32) 1770035416u); /* 9 */
	HASH_MD5_FF ( d, a, b, c, in[ 9], HASH_MD5_S12, (IUINT32) 2336552879u); /* 10 */
	HASH_MD5_FF ( c, d, a, b, in[10], HASH_MD5_S13, (IUINT32) 4294925233u); /* 11 */
	HASH_MD5_FF ( b, c, d, a, in[11], HASH_MD5_S14, (IUINT32) 2304563134u); /* 12 */
	HASH_MD5_FF ( a, b, c, d, in[12], HASH_MD5_S11, (IUINT32) 1804603682u); /* 13 */
	HASH_MD5_FF ( d, a, b, c, in[13], HASH_MD5_S12, (IUINT32) 4254626195u); /* 14 */
	HASH_MD5_FF ( c, d, a, b, in[14], HASH_MD5_S13, (IUINT32) 2792965006u); /* 15 */
	HASH_MD5_FF ( b, c, d, a, in[15], HASH_MD5_S14, (IUINT32) 1236535329u); /* 16 */

	/* Round 2 */
	HASH_MD5_GG ( a, b, c, d, in[ 1], HASH_MD5_S21, (IUINT32) 4129170786u); /* 17 */
	HASH_MD5_GG ( d, a, b, c, in[ 6], HASH_MD5_S22, (IUINT32) 3225465664u); /* 18 */
	HASH_MD5_GG ( c, d, a, b, in[11], HASH_MD5_S23, (IUINT32)  643717713u); /* 19 */
	HASH_MD5_GG ( b, c, d, a, in[ 0], HASH_MD5_S24, (IUINT32) 3921069994u); /* 20 */
	HASH_MD5_GG ( a, b, c, d, in[ 5], HASH_MD5_S21, (IUINT32) 3593408605u); /* 21 */
	HASH_MD5_GG ( d, a, b, c, in[10], HASH_MD5_S22, (IUINT32)   38016083u); /* 22 */
	HASH_MD5_GG ( c, d, a, b, in[15], HASH_MD5_S23, (IUINT32) 3634488961u); /* 23 */
	HASH_MD5_GG ( b, c, d, a, in[ 4], HASH_MD5_S24, (IUINT32) 3889429448u); /* 24 */
	HASH_MD5_GG ( a, b, c, d, in[ 9], HASH_MD5_S21, (IUINT32)  568446438u); /* 25 */
	HASH_MD5_GG ( d, a, b, c, in[14], HASH_MD5_S22, (IUINT32) 3275163606u); /* 26 */
	HASH_MD5_GG ( c, d, a, b, in[ 3], HASH_MD5_S23, (IUINT32) 4107603335u); /* 27 */
	HASH_MD5_GG ( b, c, d, a, in[ 8], HASH_MD5_S24, (IUINT32) 1163531501u); /* 28 */
	HASH_MD5_GG ( a, b, c, d, in[13], HASH_MD5_S21, (IUINT32) 2850285829u); /* 29 */
	HASH_MD5_GG ( d, a, b, c, in[ 2], HASH_MD5_S22, (IUINT32) 4243563512u); /* 30 */
	HASH_MD5_GG ( c, d, a, b, in[ 7], HASH_MD5_S23, (IUINT32) 1735328473u); /* 31 */
	HASH_MD5_GG ( b, c, d, a, in[12], HASH_MD5_S24, (IUINT32) 2368359562u); /* 32 */

	/* Round 3 */
	HASH_MD5_HH ( a, b, c, d, in[ 5], HASH_MD5_S31, (IUINT32) 4294588738u); /* 33 */
	HASH_MD5_HH ( d, a, b, c, in[ 8], HASH_MD5_S32, (IUINT32) 2272392833u); /* 34 */
	HASH_MD5_HH ( c, d, a, b, in[11], HASH_MD5_S33, (IUINT32) 1839030562u); /* 35 */
	HASH_MD5_HH ( b, c, d, a, in[14], HASH_MD5_S34, (IUINT32) 4259657740u); /* 36 */
	HASH_MD5_HH ( a, b, c, d, in[ 1], HASH_MD5_S31, (IUINT32) 2763975236u); /* 37 */
	HASH_MD5_HH ( d, a, b, c, in[ 4], HASH_MD5_S32, (IUINT32) 1272893353u); /* 38 */
	HASH_MD5_HH ( c, d, a, b, in[ 7], HASH_MD5_S33, (IUINT32) 4139469664u); /* 39 */
	HASH_MD5_HH ( b, c, d, a, in[10], HASH_MD5_S34, (IUINT32) 3200236656u); /* 40 */
	HASH_MD5_HH ( a, b, c, d, in[13], HASH_MD5_S31, (IUINT32)  681279174u); /* 41 */
	HASH_MD5_HH ( d, a, b, c, in[ 0], HASH_MD5_S32, (IUINT32) 3936430074u); /* 42 */
	HASH_MD5_HH ( c, d, a, b, in[ 3], HASH_MD5_S33, (IUINT32) 3572445317u); /* 43 */
	HASH_MD5_HH ( b, c, d, a, in[ 6], HASH_MD5_S34, (IUINT32)   76029189u); /* 44 */
	HASH_MD5_HH ( a, b, c, d, in[ 9], HASH_MD5_S31, (IUINT32) 3654602809u); /* 45 */
	HASH_MD5_HH ( d, a, b, c, in[12], HASH_MD5_S32, (IUINT32) 3873151461u); /* 46 */
	HASH_MD5_HH ( c, d, a, b, in[15], HASH_MD5_S33, (IUINT32)  530742520u); /* 47 */
	HASH_MD5_HH ( b, c, d, a, in[ 2], HASH_MD5_S34, (IUINT32) 3299628645u); /* 48 */

	/* Round 4 */
	HASH_MD5_II ( a, b, c, d, in[ 0], HASH_MD5_S41, (IUINT32) 4096336452u); /* 49 */
	HASH_MD5_II ( d, a, b, c, in[ 7], HASH_MD5_S42, (IUINT32) 1126891415u); /* 50 */
	HASH_MD5_II ( c, d, a, b, in[14], HASH_MD5_S43, (IUINT32) 2878612391u); /* 51 */
	HASH_MD5_II ( b, c, d, a, in[ 5], HASH_MD5_S44, (IUINT32) 4237533241u); /* 52 */
	HASH_MD5_II ( a, b, c, d, in[12], HASH_MD5_S41, (IUINT32) 1700485571u); /* 53 */
	HASH_MD5_II ( d, a, b, c, in[ 3], HASH_MD5_S42, (IUINT32) 2399980690u); /* 54 */
	HASH_MD5_II ( c, d, a, b, in[10], HASH_MD5_S43, (IUINT32) 4293915773u); /* 55 */
	HASH_MD5_II ( b, c, d, a, in[ 1], HASH_MD5_S44, (IUINT32) 2240044497u); /* 56 */
	HASH_MD5_II ( a, b, c, d, in[ 8], HASH_MD5_S41, (IUINT32) 1873313359u); /* 57 */
	HASH_MD5_II ( d, a, b, c, in[15], HASH_MD5_S42, (IUINT32) 4264355552u); /* 58 */
	HASH_MD5_II ( c, d, a, b, in[ 6], HASH_MD5_S43, (IUINT32) 2734768916u); /* 59 */
	HASH_MD5_II ( b, c, d, a, in[13], HASH_MD5_S44, (IUINT32) 1309151649u); /* 60 */
	HASH_MD5_II ( a, b, c, d, in[ 4], HASH_MD5_S41, (IUINT32) 4149444226u); /* 61 */
	HASH_MD5_II ( d, a, b, c, in[11], HASH_MD5_S42, (IUINT32) 3174756917u); /* 62 */
	HASH_MD5_II ( c, d, a, b, in[ 2], HASH_MD5_S43, (IUINT32)  718787259u); /* 63 */
	HASH_MD5_II ( b, c, d, a, in[ 9], HASH_MD5_S44, (IUINT32) 3951481745u); /* 64 */

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

// Set pseudoRandomNumber to zero for RFC MD5 implementation
void HASH_MD5_Init (HASH_MD5_CTX *ctx, unsigned long pseudoRandomNumber)
{
	ctx->i[0] = ctx->i[1] = (IUINT32)0;

	/* Load magic initialization constants */
	ctx->buf[0] = (IUINT32)0x67452301 + (pseudoRandomNumber * 11);
	ctx->buf[1] = (IUINT32)0xefcdab89 + (pseudoRandomNumber * 71);
	ctx->buf[2] = (IUINT32)0x98badcfe + (pseudoRandomNumber * 37);
	ctx->buf[3] = (IUINT32)0x10325476 + (pseudoRandomNumber * 97);
}

void HASH_MD5_Update(HASH_MD5_CTX *ctx, const void *input, unsigned int len)
{
	IUINT32 in[16];
	int mdi = 0;
	unsigned int i = 0, ii = 0;
	unsigned int inLen = len;
	const unsigned char *inBuf;

	inBuf = (const unsigned char*)input;

	/* Compute number of bytes mod 64 */
	mdi = (int)((ctx->i[0] >> 3) & 0x3F);

	/* Update number of bits */
	if ((ctx->i[0] + ((IUINT32)inLen << 3)) < ctx->i[0])
		ctx->i[1]++;
	ctx->i[0] += ((IUINT32)inLen << 3);
	ctx->i[1] += ((IUINT32)inLen >> 29);

	while (inLen--)
	{
		/* Add new character to buffer, increment mdi */
		ctx->in[mdi++] = *inBuf++;

		/* Transform if necessary */
		if (mdi == 0x40)
		{
			for (i = 0, ii = 0; i < 16; i++, ii += 4)
				in[i] = (((IUINT32)ctx->in[ii+3]) << 24) |
					(((IUINT32)ctx->in[ii+2]) << 16) |
					(((IUINT32)ctx->in[ii+1]) << 8) |
					((IUINT32)ctx->in[ii]);

			HASH_MD5_Transform (ctx->buf, in);
			mdi = 0;
		}
	}
}

void HASH_MD5_Final(HASH_MD5_CTX *ctx, unsigned char digest[16])
{
	IUINT32 in[16];
	int mdi = 0;
	unsigned int i = 0, ii = 0, padLen = 0;

	/* Save number of bits */
	in[14] = ctx->i[0];
	in[15] = ctx->i[1];

	/* Compute number of bytes mod 64 */
	mdi = (int)((ctx->i[0] >> 3) & 0x3F);

	/* Pad out to 56 mod 64 */
	padLen = (mdi < 56) ? (56 - mdi) : (120 - mdi);
	HASH_MD5_Update (ctx, HASH_MD5_PADDING, padLen);

	/* Append length in bits and transform */
	for (i = 0, ii = 0; i < 14; i++, ii += 4)
		in[i] = (((IUINT32)ctx->in[ii+3]) << 24) |
			(((IUINT32)ctx->in[ii+2]) << 16) |
			(((IUINT32)ctx->in[ii+1]) <<  8) |
			((IUINT32)ctx->in[ii]);
	HASH_MD5_Transform (ctx->buf, in);

	/* Store buffer in digest */
	for (i = 0, ii = 0; i < 4; i++, ii += 4)
	{
		digest[ii]   = (unsigned char)( ctx->buf[i]        & 0xFF);
		digest[ii+1] = (unsigned char)((ctx->buf[i] >>  8) & 0xFF);
		digest[ii+2] = (unsigned char)((ctx->buf[i] >> 16) & 0xFF);
		digest[ii+3] = (unsigned char)((ctx->buf[i] >> 24) & 0xFF);
	}
}


//=====================================================================
// From http://www.mirrors.wiretapped.net/security/cryptography
//=====================================================================
#define SHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* SHA1_BLK0() and SHA1_BLK() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#if !IWORDS_BIG_ENDIAN
#define SHA1_BLK0(i) (block->l[i] = (SHA1_ROL(block->l[i],24)&0xFF00FF00) \
    |(SHA1_ROL(block->l[i],8)&0x00FF00FF))
#else
#define SHA1_BLK0(i) block->l[i]
#endif
#define SHA1_BLK(i) (block->l[i&15] = SHA1_ROL(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/* (SHA1_R0+SHA1_R1), SHA1_R2, SHA1_R3, SHA1_R4 are the different operations used in SHA1 */
#define SHA1_R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+SHA1_BLK0(i)+0x5A827999+SHA1_ROL(v,5);w=SHA1_ROL(w,30);
#define SHA1_R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+SHA1_BLK(i)+0x5A827999+SHA1_ROL(v,5);w=SHA1_ROL(w,30);
#define SHA1_R2(v,w,x,y,z,i) z+=(w^x^y)+SHA1_BLK(i)+0x6ED9EBA1+SHA1_ROL(v,5);w=SHA1_ROL(w,30);
#define SHA1_R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+SHA1_BLK(i)+0x8F1BBCDC+SHA1_ROL(v,5);w=SHA1_ROL(w,30);
#define SHA1_R4(v,w,x,y,z,i) z+=(w^x^y)+SHA1_BLK(i)+0xCA62C1D6+SHA1_ROL(v,5);w=SHA1_ROL(w,30);


/* Hash a single 512-bit block. This is the core of the algorithm. */
void HASH_SHA1_Transform(IUINT32 state[5], const unsigned char *buffer)
{
	IUINT32 a, b, c, d, e;
	typedef struct {
		IUINT32 l[16];
	}	CHAR64LONG16;
	CHAR64LONG16 blk;
	CHAR64LONG16 *block = &blk;
	for (e = 0; e < 16; e++) {
		a = buffer[(e << 2) + 0];
		b = buffer[(e << 2) + 1];
		c = buffer[(e << 2) + 2];
		d = buffer[(e << 2) + 3];
		block->l[e] = a | (b << 8) | (c << 16) | (d << 24);
	}
    block = (CHAR64LONG16*)buffer;
    /* Copy ctx->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    /* 4 rounds of 20 operations each. Loop unSHA1_ROLled. */
    SHA1_R0(a,b,c,d,e, 0); SHA1_R0(e,a,b,c,d, 1); SHA1_R0(d,e,a,b,c, 2); SHA1_R0(c,d,e,a,b, 3);
    SHA1_R0(b,c,d,e,a, 4); SHA1_R0(a,b,c,d,e, 5); SHA1_R0(e,a,b,c,d, 6); SHA1_R0(d,e,a,b,c, 7);
    SHA1_R0(c,d,e,a,b, 8); SHA1_R0(b,c,d,e,a, 9); SHA1_R0(a,b,c,d,e,10); SHA1_R0(e,a,b,c,d,11);
    SHA1_R0(d,e,a,b,c,12); SHA1_R0(c,d,e,a,b,13); SHA1_R0(b,c,d,e,a,14); SHA1_R0(a,b,c,d,e,15);
    SHA1_R1(e,a,b,c,d,16); SHA1_R1(d,e,a,b,c,17); SHA1_R1(c,d,e,a,b,18); SHA1_R1(b,c,d,e,a,19);
    SHA1_R2(a,b,c,d,e,20); SHA1_R2(e,a,b,c,d,21); SHA1_R2(d,e,a,b,c,22); SHA1_R2(c,d,e,a,b,23);
    SHA1_R2(b,c,d,e,a,24); SHA1_R2(a,b,c,d,e,25); SHA1_R2(e,a,b,c,d,26); SHA1_R2(d,e,a,b,c,27);
    SHA1_R2(c,d,e,a,b,28); SHA1_R2(b,c,d,e,a,29); SHA1_R2(a,b,c,d,e,30); SHA1_R2(e,a,b,c,d,31);
    SHA1_R2(d,e,a,b,c,32); SHA1_R2(c,d,e,a,b,33); SHA1_R2(b,c,d,e,a,34); SHA1_R2(a,b,c,d,e,35);
    SHA1_R2(e,a,b,c,d,36); SHA1_R2(d,e,a,b,c,37); SHA1_R2(c,d,e,a,b,38); SHA1_R2(b,c,d,e,a,39);
    SHA1_R3(a,b,c,d,e,40); SHA1_R3(e,a,b,c,d,41); SHA1_R3(d,e,a,b,c,42); SHA1_R3(c,d,e,a,b,43);
    SHA1_R3(b,c,d,e,a,44); SHA1_R3(a,b,c,d,e,45); SHA1_R3(e,a,b,c,d,46); SHA1_R3(d,e,a,b,c,47);
    SHA1_R3(c,d,e,a,b,48); SHA1_R3(b,c,d,e,a,49); SHA1_R3(a,b,c,d,e,50); SHA1_R3(e,a,b,c,d,51);
    SHA1_R3(d,e,a,b,c,52); SHA1_R3(c,d,e,a,b,53); SHA1_R3(b,c,d,e,a,54); SHA1_R3(a,b,c,d,e,55);
    SHA1_R3(e,a,b,c,d,56); SHA1_R3(d,e,a,b,c,57); SHA1_R3(c,d,e,a,b,58); SHA1_R3(b,c,d,e,a,59);
    SHA1_R4(a,b,c,d,e,60); SHA1_R4(e,a,b,c,d,61); SHA1_R4(d,e,a,b,c,62); SHA1_R4(c,d,e,a,b,63);
    SHA1_R4(b,c,d,e,a,64); SHA1_R4(a,b,c,d,e,65); SHA1_R4(e,a,b,c,d,66); SHA1_R4(d,e,a,b,c,67);
    SHA1_R4(c,d,e,a,b,68); SHA1_R4(b,c,d,e,a,69); SHA1_R4(a,b,c,d,e,70); SHA1_R4(e,a,b,c,d,71);
    SHA1_R4(d,e,a,b,c,72); SHA1_R4(c,d,e,a,b,73); SHA1_R4(b,c,d,e,a,74); SHA1_R4(a,b,c,d,e,75);
    SHA1_R4(e,a,b,c,d,76); SHA1_R4(d,e,a,b,c,77); SHA1_R4(c,d,e,a,b,78); SHA1_R4(b,c,d,e,a,79);
    /* Add the working vars back into ctx.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    /* Wipe variables */
    a = b = c = d = e = 0;

    (void)a; (void)b; (void)c; (void)d; (void)e;
}


/* HASH_SHA1_Init - Initialize new ctx */
void HASH_SHA1_Init(HASH_SHA1_CTX* ctx)
{
    /* SHA1 initialization constants */
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}


/* Run your data through this. */
void HASH_SHA1_Update(HASH_SHA1_CTX* ctx, const void *input, unsigned int len)
{
	const unsigned char *data = (const unsigned char*)input;
	IUINT32 i, j;
    j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += len << 3) < (len << 3)) ctx->count[1]++;
    ctx->count[1] += (len >> 29);
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64-j));
        HASH_SHA1_Transform(ctx->state, ctx->buffer);
        for ( ; i + 63 < len; i += 64) {
            HASH_SHA1_Transform(ctx->state, data + i);
        }
        j = 0;
    }
    else i = 0;
    memcpy(&ctx->buffer[j], &data[i], len - i);
}


/* Add padding and return the message digest. */
void HASH_SHA1_Final(HASH_SHA1_CTX* ctx, unsigned char digest[20])
{
	IUINT32 i, j;
	unsigned char finalcount[8];
    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((ctx->count[(i >= 4 ? 0 : 1)]
         >> ((3-(i & 3)) * 8) ) & 255);  /* Endian independent */
    }
    HASH_SHA1_Update(ctx, (unsigned char *)"\200", 1);
    while ((ctx->count[0] & 504) != 448) {
        HASH_SHA1_Update(ctx, (unsigned char *)"\0", 1);
    }
    HASH_SHA1_Update(ctx, finalcount, 8);  /* Should cause a HASH_SHA1_Transform() */
    for (i = 0; i < 20; i++) {
        digest[i] = (unsigned char)
         ((ctx->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
    }
    /* Wipe variables */
    i = j = 0;
    (void)i; (void)j;
    memset(ctx->buffer, 0, 64);
    memset(ctx->state, 0, 20);
    memset(ctx->count, 0, 8);
    memset(&finalcount, 0, 8);
}



//=====================================================================
// UTILITIES
//=====================================================================
char* hash_digest_to_string(char *out, const unsigned char *in, int size)
{
	static const char hex[17] = "0123456789abcdef";
	static char buffer[96];
	char *ptr = out;
	if (out == NULL) out = buffer;
	for (; 0 < size; size--) {
		unsigned char ch = *in++;
		*ptr++ = hex[ch >> 4];
		*ptr++ = hex[ch & 15];
	}
	*ptr++ = 0;
	return out;
}

// calculate md5sum and convert digests to string
char* hash_md5sum(char *out, const void *in, unsigned int len)
{
	static char text[48];
	unsigned char digest[16];
	HASH_MD5_CTX ctx;
	HASH_MD5_Init(&ctx, 0);
	HASH_MD5_Update(&ctx, in, (unsigned int)len);
	HASH_MD5_Final(&ctx, digest);
	if (out == NULL) out = text;
	return hash_digest_to_string(out, digest, 16);
}

// calculate sha1sum and convert digests to string
char* hash_sha1sum(char *out, const void *in, unsigned int len)
{
	static char text[48];
	unsigned char digest[20];
	HASH_SHA1_CTX ctx;
	HASH_SHA1_Init(&ctx);
	HASH_SHA1_Update(&ctx, in, len);
	HASH_SHA1_Final(&ctx, digest);
	if (out == NULL) out = text;
	return hash_digest_to_string(out, digest, 20);
}

// crc32

/* Need an unsigned type capable of holding 32 bits; */
#define UPDC32(octet, crc) (hash_crc32_table[((crc) ^ (octet)) & 0xff] ^ ((crc) >> 8))

const IUINT32 hash_crc32_table[] = { /* CRC polynomial 0xedb88320 */
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// calculate crc32 and return result
IUINT32 hash_crc32(const void *in, unsigned int len)
{
	const unsigned char* p = (const unsigned char *)in;
	IUINT32 result = 0xffffffff;
	unsigned int i = 0;
	for (i = 0; i < len; i++){
		result = UPDC32(p[i], result);
	}
	result ^= 0xffffffff;  
	return result;
}

// sum all bytes together
IUINT32 hash_checksum(const void *in, unsigned int len)
{
	const unsigned char* p = (const unsigned char *)in;
	IUINT32 checksum = 0;
	for (; len > 0; len--, p++) checksum += *p;
	return checksum;
}


//=====================================================================
// Diffie-Hellman key exchange
//=====================================================================

// Russian Peasant multiplication
// http://en.wikipedia.org/wiki/Ancient_Egyptian_multiplication
// returns (a * b) % c
static inline IUINT64 DH_MulMod(IUINT64 a, IUINT64 b, IUINT64 c)
{
	IUINT64 x = 0;
#if (!defined(__GNUC__)) || (!defined(__amd64__))
	if (a >= c) a %= c;
	if (b >= c) b %= c;
	if (a == 0) return 0;
	if (b == 0) return 0;
	if (a <= b) { x = a; a = b; b = x; x = 0; }
	while (b) {
		if (b & 1) {
			IUINT64 t = c - a;
			if ( x >= t) x -= t;
			else x += a;
		}
		if (a >= c - a) a = a + a - c;
		else a = a + a;
		b >>= 1;
	}
#else	// 10x times faster in amd64
	__asm__ __volatile__ (
		"mov %1, %%rax;\n"   
		"mul %2;\n"      
		"div %3;\n" 
		"mov %%rdx, %0;\n"
		:"=r"(x) 
		:"r"(a), "r"(b), "r"(c)
		:"%rax", "%rdx", "memory"
	);
#endif
	return x;
}

// Right-to-left binary method
// http://en.wikipedia.org/wiki/Modular_exponentiation
// calculate (a ^ b) % c
IUINT64 DH_PowerMod(IUINT64 a, IUINT64 b, IUINT64 c)
{
	IUINT64 x = 1;
	if (a >= c) a %= c;
	if (b >= c) b %= c;
	if (b == 0) return 1;
	if (b == 1) return a;
	if (a == 0) return 0;
	if (a == 1) return 1;
	if (c == 0) return 0;
	while (b > 0) {
        if (b & 1) x = DH_MulMod(x, a, c);
		a = DH_MulMod(a, a, c);
		b >>= 1;
	}
	return x;
}

// returns random local key
IUINT64 DH_Random()
{
	IUINT64 x = ((rand() << 16) ^ rand()) & 0x7fffffff;
	IUINT64 y = ((rand() << 16) ^ rand()) & 0x7fffffff;
	return (x << 32) | y;
}

// calculate A/B which will be sent to remote
IUINT64 DH_Exchange(IUINT64 local)
{
	IUINT64 x = 0x7fffffff;
	IUINT64 y = 0xffffffe7;
	IUINT64 p = (x << 32) | y;	// 	0x7fffffffffffffe7L
	return DH_PowerMod(5, local, p);
}

// get final symmetrical-key from local key and remote A/B
IUINT64 DH_Key(IUINT64 local, IUINT64 remote)
{
	IUINT64 x = 0x7fffffff;
	IUINT64 y = 0xffffffe7;
	IUINT64 p = (x << 32) | y;	// 	0x7fffffffffffffe7L
	return DH_PowerMod(remote, local, p);
}

// get qword from hex string 
void DH_STR_TO_U64(const char *str, IUINT64 *x)
{
	IUINT64 a = 0, b;
	int i;
	for (i = 0; str[i]; i++) {
		char c = str[i];
		if (c >= '0' && c <= '9') b = c - '0';
		else if (c >= 'A' && c <= 'F') b = c - 'A' + 10;
		else if (c >= 'a' && c <= 'f') b = c - 'a' + 10;
		else b = 0;
		a = (a << 4) | b;
	}
	x[0] = a;
}

// hex string from qword, capacity of str must above 17
void DH_U64_TO_STR(IUINT64 x, char *str)
{
	static const char hex[] = "0123456789abcdef";
	int i;
	for (i = 0; i < 16; i++) {
		int y = (int)((x >> ((15 - i) * 4)) & 0xf);
		str[i] = hex[y];
	}
	str[16] = 0;
}


//=====================================================================
// CRYPTO RC4
//=====================================================================

// Initialize RC4 context with key
void CRYPTO_RC4_Init(CRYPTO_RC4_CTX *ctx, const void *key, int keylen)
{
	int X, Y, i, j, k, a;
	unsigned char *box;
	box = ctx->box;
	if (keylen <= 0 || key == NULL) {
		X = -1;
		Y = -1;
	}	else {
		X = Y = j = k = 0;
		for (i = 0; i < 256; i++) 
			box[i] = (unsigned char)i;
		for (i = 0; i < 256; i++) {
			a = box[i];
			j = (unsigned char)(j + a + ((const unsigned char*)key)[k]);
			box[i] = box[j];
			box[j] = a;
			if (++k >= keylen) k = 0;
		}
	}
	ctx->x = X;
	ctx->y = Y;
}

// Apply RC4 to data
void CRYPTO_RC4_Update(CRYPTO_RC4_CTX *ctx, void *out, const void *in, size_t size)
{
	const unsigned char *src = (const unsigned char*)in;
	unsigned char *dst = (unsigned char*)out;
	unsigned char *box = ctx->box;
	int X = ctx->x;
	int Y = ctx->y;
	if (X < 0 || Y < 0) {			/* no crypt */
		if (src != dst) 
			memmove(dst, src, size);
	}	else {						/* crypt */
		int a, b; 
		for (; size > 0; src++, dst++, size--) {
			X = (unsigned char)(X + 1);
			a = box[X];
			Y = (unsigned char)(Y + a);
			box[X] = box[Y];
			b = box[Y];
			box[Y] = a;
			dst[0] = src[0] ^ box[(unsigned char)(a + b)];
		}
		ctx->x = X;
		ctx->y = Y;
	}
}

// Apply RC4 to data (in-place)
void CRYPTO_RC4_Direct(const void *key, int keylen, void *out,
		const void *in, size_t size, int ntimes)
{
	const void *src = in;
	CRYPTO_RC4_CTX ctx;
	CRYPTO_RC4_Init(&ctx, key, keylen);
	for (; ntimes > 0; ntimes--) {
		CRYPTO_RC4_Update(&ctx, out, src, size);
		src = out;
	}
}


//=====================================================================
// CRYPTO chacha20:
//=====================================================================

static void cipher_chacha20_init_block(CRYPTO_CHACHA20_CTX *ctx, 
		const IUINT8 key[], const IUINT8 nonce[])
{
	const IUINT8 *magic_constant = (const IUINT8*)"expand 32-byte k";
	ctx->state[0] = is_pack4(magic_constant + 0 * 4);
	ctx->state[1] = is_pack4(magic_constant + 1 * 4);
	ctx->state[2] = is_pack4(magic_constant + 2 * 4);
	ctx->state[3] = is_pack4(magic_constant + 3 * 4);
	ctx->state[4] = is_pack4(key + 0 * 4);
	ctx->state[5] = is_pack4(key + 1 * 4);
	ctx->state[6] = is_pack4(key + 2 * 4);
	ctx->state[7] = is_pack4(key + 3 * 4);
	ctx->state[8] = is_pack4(key + 4 * 4);
	ctx->state[9] = is_pack4(key + 5 * 4);
	ctx->state[10] = is_pack4(key + 6 * 4);
	ctx->state[11] = is_pack4(key + 7 * 4);
	ctx->state[12] = 0;		// counter
	ctx->state[13] = is_pack4(nonce + 0 * 4);
	ctx->state[14] = is_pack4(nonce + 1 * 4);
	ctx->state[15] = is_pack4(nonce + 2 * 4);
}

static void cipher_chacha20_qround(IUINT32 *x, int a, int b, int c, int d)
{
	x[a] += x[b]; x[d] = is_rotl32(x[d] ^ x[a], 16); 
	x[c] += x[d]; x[b] = is_rotl32(x[b] ^ x[c], 12);
	x[a] += x[b]; x[d] = is_rotl32(x[d] ^ x[a], 8); 
	x[c] += x[d]; x[b] = is_rotl32(x[b] ^ x[c], 7); 
}

static void cipher_chacha20_block_next(CRYPTO_CHACHA20_CTX *ctx) {
	IUINT32 x[16];
	int i;

	// This is where the crazy voodoo magic happens.
	// Mix the bytes a lot and hope that nobody finds out how to undo it.
	for (i = 0; i < 16; i++) 
		x[i] = ctx->state[i];

	for (i = 0; i < 10; i++) {
		cipher_chacha20_qround(x, 0, 4, 8, 12);
		cipher_chacha20_qround(x, 1, 5, 9, 13);
		cipher_chacha20_qround(x, 2, 6, 10, 14);
		cipher_chacha20_qround(x, 3, 7, 11, 15);
		cipher_chacha20_qround(x, 0, 5, 10, 15);
		cipher_chacha20_qround(x, 1, 6, 11, 12);
		cipher_chacha20_qround(x, 2, 7, 8, 13);
		cipher_chacha20_qround(x, 3, 4, 9, 14);
	}

	for (i = 0; i < 16; i++) 
		x[i] += ctx->state[i];

	for (i = 0; i < 16; i++) 
		is_unpack4(x[i], ctx->keystream + i * 4);

	// increment counter
	ctx->state[12]++;
}

void CRYPTO_CHACHA20_Init(CRYPTO_CHACHA20_CTX *ctx, 
		const IUINT8 *key, const IUINT8 *nonce, IUINT32 counter)
{
	memset(ctx, 0, sizeof(CRYPTO_CHACHA20_CTX));
	cipher_chacha20_init_block(ctx, key, nonce);
	ctx->state[12] = counter;
	ctx->position = 64;
}

void CRYPTO_CHACHA20_Update(CRYPTO_CHACHA20_CTX *ctx, void *out, 
		const void *in, size_t size)
{
	const IUINT8 *src = (const IUINT8*)in;
	IUINT8 *dst = (IUINT8*)out;
	size_t i;
	for (i = size; i > 0; src++, dst++, i--) {
		if (ctx->position >= 64) {
			cipher_chacha20_block_next(ctx);
			ctx->position = 0;
		}
		dst[0] = src[0] ^ ctx->keystream[ctx->position];
		ctx->position++;
	}
}


//=====================================================================
// CRYPTO XTEA: https://en.wikipedia.org/wiki/XTEA
//=====================================================================
void CRYPTO_XTEA_Encipher(int nrounds, const IUINT32 key[4], IUINT32 v[2])
{
	IUINT32 v0 = v[0], v1 = v[1], sum = 0, delta = 0x9E3779B9;
	for (; nrounds > 0; nrounds--) {
	v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
	sum += delta;
	v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
	}
	v[0] = v0; v[1] = v1;
}

void CRYPTO_XTEA_Decipher(int nrounds, const IUINT32 key[4], IUINT32 v[2])
{
	IUINT32 v0 = v[0], v1 = v[1], delta = 0x9E3779B9;
	IUINT32 sum = delta * ((IUINT32)nrounds);
	for (; nrounds > 0; nrounds--) {
		v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
		sum -= delta;
		v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
	}
	v[0]=v0; v[1]=v1;
}


//=====================================================================
// AES: Advanced Encryption Standard (block-wise encrypt/decrypt)
//=====================================================================

// AES S-box: substitution box
static const IUINT8 aes_sbox[] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

// AES Inverse S-box: substitution box
static const IUINT8 aes_inv_sbox[] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

#define AES_KEYSIZE_128		16
#define AES_KEYSIZE_192		24
#define AES_KEYSIZE_256		32

// Rotate right 32-bit word
static inline IUINT32 crypto_mul_by_x(IUINT32 w) {
	IUINT32 x = w & 0x7f7f7f7f;
	IUINT32 y = w & 0x80808080;
	// multiply by polynomial 'x' (0b10) in GF(2^8)
	return (x << 1) ^ (y >> 7) * 0x1b;
}

// Multiply by x^2 in GF(2^8)
static inline IUINT32 crypto_mul_by_x2(IUINT32 w) {
	IUINT32 x = w & 0x3f3f3f3f;
	IUINT32 y = w & 0x80808080;
	IUINT32 z = w & 0x40404040;
	// multiply by polynomial 'x^2' (0b100) in GF(2^8)
	return (x << 2) ^ (y >> 7) * 0x36 ^ (z >> 6) * 0x1b;
}

// Rotate right 32-bit word
static inline IUINT32 crypto_ror32(IUINT32 word, unsigned int shift) {
	return (word >> (shift & 31)) | (word << (32 - (shift & 31)));
}

// MixColumns transformation
static inline IUINT32 crypto_mix_columns(IUINT32 x) {
	IUINT32 y = crypto_mul_by_x(x) ^ crypto_ror32(x, 16);
	return y ^ crypto_ror32(x ^ y, 8);
}

// Inverse MixColumns transformation
static IUINT32 crypto_inv_mix_columns(IUINT32 x) {
	IUINT32 y = crypto_mul_by_x2(x);
	return crypto_mix_columns(x ^ y ^ crypto_ror32(y, 16));
}

// SubBytes and ShiftRows transformation
static inline IUINT32 crypto_subshift(IUINT32 *in, int pos) {
	return (aes_sbox[in[pos] & 0xff]) ^
	       (aes_sbox[(in[(pos + 1) % 4] >>  8) & 0xff] <<  8) ^
	       (aes_sbox[(in[(pos + 2) % 4] >> 16) & 0xff] << 16) ^
	       (aes_sbox[(in[(pos + 3) % 4] >> 24) & 0xff] << 24);
}

// Inverse SubBytes and ShiftRows transformation
static inline IUINT32 crypto_inv_subshift(IUINT32 in[], int pos) {
	return (aes_inv_sbox[in[pos] & 0xff]) ^
	       (aes_inv_sbox[(in[(pos + 3) % 4] >>  8) & 0xff] <<  8) ^
	       (aes_inv_sbox[(in[(pos + 2) % 4] >> 16) & 0xff] << 16) ^
	       (aes_inv_sbox[(in[(pos + 1) % 4] >> 24) & 0xff] << 24);
}

// SubWord transformation
static inline IUINT32 crypto_subw(IUINT32 in) {
	return (aes_sbox[in & 0xff]) ^
	       (aes_sbox[(in >>  8) & 0xff] <<  8) ^
	       (aes_sbox[(in >> 16) & 0xff] << 16) ^
	       (aes_sbox[(in >> 24) & 0xff] << 24);
}

// get unaligned little-endian uint32
static inline IUINT32 crypto_get_le32(const void *p) {
	IUINT32 x;
	is_decode32u_lsb(p, &x);
	return x;
}

// put unaligned little-endian uint32
static inline void crypto_put_le32(IUINT32 x, void *p) {
	is_encode32u_lsb(p, x);
}

// keylen: 16, 24, 32 bytes
void CRYPTO_AES_Init(CRYPTO_AES_CTX *ctx, const IUINT8 *key, IUINT32 keylen)
{
	IUINT32 kwords, rc, i, j;
	IUINT8 keycache[32];

	if (keylen != 16 && keylen != 24 && keylen != 32) {
		if (keylen > 0 && key != NULL) {
			int pos = 0;
			for (; pos < 32; ) {
				int canwrite = 32 - pos;
				int canread = keylen;
				int n = (canwrite < canread) ? canwrite : canread;
				memcpy(keycache + pos, key, n);
				pos += n;
			}
			key = keycache;
			if (keylen <= 16) keylen = 16;
			else if (keylen <= 24) keylen = 24;
			else keylen = 32;
		} else {
			memset(keycache, 0, 32);
			key = keycache;
			keylen = 16;
		}
	}

	memset(ctx, 0, sizeof(CRYPTO_AES_CTX));

	ctx->key_length = keylen;
	kwords = keylen / sizeof(IUINT32);

	for (i = 0; i < kwords; i++)
		ctx->key_enc[i] = crypto_get_le32(key + i * sizeof(IUINT32));

	for (i = 0, rc = 1; i < 10; i++, rc = crypto_mul_by_x(rc)) {
		IUINT32 *rki = ctx->key_enc + (i * kwords);
		IUINT32 *rko = rki + kwords;

		rko[0] = crypto_ror32(crypto_subw(rki[kwords - 1]), 8) ^ rc ^ rki[0];
		rko[1] = rko[0] ^ rki[1];
		rko[2] = rko[1] ^ rki[2];
		rko[3] = rko[2] ^ rki[3];

		if (keylen == AES_KEYSIZE_192) {
			if (i >= 7)
				break;
			rko[4] = rko[3] ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
		} else if (keylen == AES_KEYSIZE_256) {
			if (i >= 6)
				break;
			rko[4] = crypto_subw(rko[3]) ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
			rko[6] = rko[5] ^ rki[6];
			rko[7] = rko[6] ^ rki[7];
		}
	}

	// generate decryption keys
	ctx->key_dec[0] = ctx->key_enc[keylen + 24];
	ctx->key_dec[1] = ctx->key_enc[keylen + 25];
	ctx->key_dec[2] = ctx->key_enc[keylen + 26];
	ctx->key_dec[3] = ctx->key_enc[keylen + 27];

	for (i = 4, j = keylen + 20; j > 0; i += 4, j -= 4) {
		ctx->key_dec[i]     = crypto_inv_mix_columns(ctx->key_enc[j]);
		ctx->key_dec[i + 1] = crypto_inv_mix_columns(ctx->key_enc[j + 1]);
		ctx->key_dec[i + 2] = crypto_inv_mix_columns(ctx->key_enc[j + 2]);
		ctx->key_dec[i + 3] = crypto_inv_mix_columns(ctx->key_enc[j + 3]);
	}

	ctx->key_dec[i]     = ctx->key_enc[0];
	ctx->key_dec[i + 1] = ctx->key_enc[1];
	ctx->key_dec[i + 2] = ctx->key_enc[2];
	ctx->key_dec[i + 3] = ctx->key_enc[3];
}

// encrypt a single AES block (16 bytes)
void CRYPTO_AES_Encrypt(CRYPTO_AES_CTX *ctx, IUINT8 *out, const IUINT8 *in)
{
	const IUINT32 *rkp = ctx->key_enc + 4;
	int rounds = 6 + ctx->key_length / 4;
	IUINT32 st0[4], st1[4];
	int round;

	// initial AddRoundKey
	st0[0] = ctx->key_enc[0] ^ crypto_get_le32(in);
	st0[1] = ctx->key_enc[1] ^ crypto_get_le32(in + 4);
	st0[2] = ctx->key_enc[2] ^ crypto_get_le32(in + 8);
	st0[3] = ctx->key_enc[3] ^ crypto_get_le32(in + 12);

	// Force the compiler to emit data independent Sbox references
	st0[0] ^= aes_sbox[ 0] ^ aes_sbox[ 64] ^ aes_sbox[134] ^ aes_sbox[195];
	st0[1] ^= aes_sbox[16] ^ aes_sbox[ 82] ^ aes_sbox[158] ^ aes_sbox[221];
	st0[2] ^= aes_sbox[32] ^ aes_sbox[ 96] ^ aes_sbox[160] ^ aes_sbox[234];
	st0[3] ^= aes_sbox[48] ^ aes_sbox[112] ^ aes_sbox[186] ^ aes_sbox[241];

	// Main rounds: SubBytes, ShiftRows, MixColumns, AddRoundKey
	for (round = 0;; round += 2, rkp += 8) {
		st1[0] = crypto_mix_columns(crypto_subshift(st0, 0)) ^ rkp[0];
		st1[1] = crypto_mix_columns(crypto_subshift(st0, 1)) ^ rkp[1];
		st1[2] = crypto_mix_columns(crypto_subshift(st0, 2)) ^ rkp[2];
		st1[3] = crypto_mix_columns(crypto_subshift(st0, 3)) ^ rkp[3];

		if (round == rounds - 2)
			break;

		st0[0] = crypto_mix_columns(crypto_subshift(st1, 0)) ^ rkp[4];
		st0[1] = crypto_mix_columns(crypto_subshift(st1, 1)) ^ rkp[5];
		st0[2] = crypto_mix_columns(crypto_subshift(st1, 2)) ^ rkp[6];
		st0[3] = crypto_mix_columns(crypto_subshift(st1, 3)) ^ rkp[7];
	}

	crypto_put_le32(crypto_subshift(st1, 0) ^ rkp[4], out);
	crypto_put_le32(crypto_subshift(st1, 1) ^ rkp[5], out + 4);
	crypto_put_le32(crypto_subshift(st1, 2) ^ rkp[6], out + 8);
	crypto_put_le32(crypto_subshift(st1, 3) ^ rkp[7], out + 12);
}

// decrypt a single AES block (16 bytes)
void CRYPTO_AES_Decrypt(CRYPTO_AES_CTX *ctx, IUINT8 *out, const IUINT8 *in)
{
	const IUINT32 *rkp = ctx->key_dec + 4;
	int rounds = 6 + ctx->key_length / 4;
	IUINT32 st0[4], st1[4];
	int round;

	// initial AddRoundKey
	st0[0] = ctx->key_dec[0] ^ crypto_get_le32(in);
	st0[1] = ctx->key_dec[1] ^ crypto_get_le32(in + 4);
	st0[2] = ctx->key_dec[2] ^ crypto_get_le32(in + 8);
	st0[3] = ctx->key_dec[3] ^ crypto_get_le32(in + 12);

	// Force the compiler to emit data independent Sbox references
	st0[0] ^= aes_inv_sbox[ 0] ^ aes_inv_sbox[ 64] ^ aes_inv_sbox[129] ^ aes_inv_sbox[200];
	st0[1] ^= aes_inv_sbox[16] ^ aes_inv_sbox[ 83] ^ aes_inv_sbox[150] ^ aes_inv_sbox[212];
	st0[2] ^= aes_inv_sbox[32] ^ aes_inv_sbox[ 96] ^ aes_inv_sbox[160] ^ aes_inv_sbox[236];
	st0[3] ^= aes_inv_sbox[48] ^ aes_inv_sbox[112] ^ aes_inv_sbox[187] ^ aes_inv_sbox[247];

	// Main rounds: InvSubBytes, InvShiftRows, InvMixColumns, AddRoundKey
	for (round = 0;; round += 2, rkp += 8) {
		st1[0] = crypto_inv_mix_columns(crypto_inv_subshift(st0, 0)) ^ rkp[0];
		st1[1] = crypto_inv_mix_columns(crypto_inv_subshift(st0, 1)) ^ rkp[1];
		st1[2] = crypto_inv_mix_columns(crypto_inv_subshift(st0, 2)) ^ rkp[2];
		st1[3] = crypto_inv_mix_columns(crypto_inv_subshift(st0, 3)) ^ rkp[3];

		if (round == rounds - 2)
			break;

		st0[0] = crypto_inv_mix_columns(crypto_inv_subshift(st1, 0)) ^ rkp[4];
		st0[1] = crypto_inv_mix_columns(crypto_inv_subshift(st1, 1)) ^ rkp[5];
		st0[2] = crypto_inv_mix_columns(crypto_inv_subshift(st1, 2)) ^ rkp[6];
		st0[3] = crypto_inv_mix_columns(crypto_inv_subshift(st1, 3)) ^ rkp[7];
	}

	crypto_put_le32(crypto_inv_subshift(st1, 0) ^ rkp[4], out);
	crypto_put_le32(crypto_inv_subshift(st1, 1) ^ rkp[5], out + 4);
	crypto_put_le32(crypto_inv_subshift(st1, 2) ^ rkp[6], out + 8);
	crypto_put_le32(crypto_inv_subshift(st1, 3) ^ rkp[7], out + 12);
}



//=====================================================================
// AES-GCM: stream-based authenticated encryption
//=====================================================================
#define GCM_BLOCK_SIZE 16

// store big-endian uint64
static inline void crypto_store_be64(IUINT64 value, IUINT8 *out) {
	int i;
	for (i = 0; i < 8; ++i) {
		out[i] = (IUINT8)(value >> (56 - i * 8));
	}
}

// XOR two blocks of GCM_BLOCK_SIZE bytes
static inline void crypto_gcm_block_xor(IUINT8 *dst, const IUINT8 *src) {
	int i;
	for (i = 0; i < GCM_BLOCK_SIZE; ++i) {
		dst[i] ^= src[i];
	}
}

// Galois field multiplication: X = X * Y
static void crypto_gcm_mul(IUINT8 *X, const IUINT8 *Y) {
	IUINT8 Z[GCM_BLOCK_SIZE];
	IUINT8 V[GCM_BLOCK_SIZE];
	int i;
	memset(Z, 0, sizeof(Z));
	memcpy(V, Y, sizeof(V));
	for (i = 0; i < GCM_BLOCK_SIZE; ++i) {
		int bit;
		IUINT8 x = X[i];
		for (bit = 7; bit >= 0; --bit) {
			if ((x >> bit) & 1) {
				int j;
				for (j = 0; j < GCM_BLOCK_SIZE; ++j) {
					Z[j] ^= V[j];
				}
			}
			{
				IUINT8 lsb = V[GCM_BLOCK_SIZE - 1] & 1u;
				int j;
				for (j = GCM_BLOCK_SIZE - 1; j > 0; --j) {
					V[j] = (IUINT8)((V[j] >> 1) | ((V[j - 1] & 1u) << 7));
				}
				V[0] >>= 1;
				if (lsb) {
					V[0] ^= 0xe1u;
				}
			}
		}
	}
	memcpy(X, Z, sizeof(Z));
}

// Increment the rightmost 32 bits of the counter
static inline void crypto_gcm_inc32(IUINT8 *counter) {
	int i;
	for (i = GCM_BLOCK_SIZE - 1; i >= GCM_BLOCK_SIZE - 4; --i) {
		counter[i] += 1u;
		if (counter[i] != 0) {
			break;
		}
	}
}

// Generate next keystream block
static void crypto_gcm_gen_keystream(CRYPTO_GCM_CTX *ctx) {
	crypto_gcm_inc32(ctx->counter);
	CRYPTO_AES_Encrypt(&ctx->aes, ctx->keystream, ctx->counter);
	ctx->keystream_used = 0;
}

// Feed data into GCM authentication
static void crypto_gcm_feed_bytes(CRYPTO_GCM_CTX *ctx, const IUINT8 *data,
		size_t len, IUINT8 *buffer, IUINT32 *buffer_len)
{
	while (len > 0) {
		size_t space = (size_t)GCM_BLOCK_SIZE - (size_t)(*buffer_len);
		size_t step = (len < space) ? len : space;
		memcpy(buffer + *buffer_len, data, step);
		*buffer_len += (IUINT32)step;
		data += step;
		len -= step;
		if (*buffer_len == GCM_BLOCK_SIZE) {
			crypto_gcm_block_xor(ctx->auth, buffer);
			crypto_gcm_mul(ctx->auth, ctx->H);
			memset(buffer, 0, GCM_BLOCK_SIZE);
			*buffer_len = 0;
		}
	}
}

// Pad the buffer to a full block and process it
static void crypto_gcm_pad_buffer(CRYPTO_GCM_CTX *ctx, IUINT8 *buffer,
		IUINT32 *buffer_len)
{
	if (*buffer_len == 0) {
		return;
	}
	{
		IUINT8 block[GCM_BLOCK_SIZE];
		memcpy(block, buffer, *buffer_len);
		memset(block + *buffer_len, 0, GCM_BLOCK_SIZE - *buffer_len);
		crypto_gcm_block_xor(ctx->auth, block);
		crypto_gcm_mul(ctx->auth, ctx->H);
	}
	memset(buffer, 0, GCM_BLOCK_SIZE);
	*buffer_len = 0;
}

// Finalize AAD processing
static void crypto_gcm_finalize_aad(CRYPTO_GCM_CTX *ctx) {
	if (ctx->aad_finalized) {
		return;
	}
	crypto_gcm_pad_buffer(ctx, ctx->aad_buf, &ctx->aad_buf_len);
	ctx->aad_finalized = 1;
}

// Setup initial counter J0 from IV
static void crypto_gcm_setup_j0(CRYPTO_GCM_CTX *ctx, const IUINT8 *iv,
		size_t iv_len)
{
	if (iv_len == 12) {
		memcpy(ctx->J0, iv, 12);
		ctx->J0[12] = 0;
		ctx->J0[13] = 0;
		ctx->J0[14] = 0;
		ctx->J0[15] = 1;
		return;
	}
	{
		IUINT8 acc[GCM_BLOCK_SIZE];
		IUINT8 block[GCM_BLOCK_SIZE];
		size_t remaining = iv_len;
		size_t copied = 0;
		memset(acc, 0, sizeof(acc));
		while (remaining >= GCM_BLOCK_SIZE) {
			memcpy(block, iv + copied, GCM_BLOCK_SIZE);
			crypto_gcm_block_xor(acc, block);
			crypto_gcm_mul(acc, ctx->H);
			remaining -= GCM_BLOCK_SIZE;
			copied += GCM_BLOCK_SIZE;
		}
		if (remaining > 0) {
			memset(block, 0, GCM_BLOCK_SIZE);
			memcpy(block, iv + copied, remaining);
			crypto_gcm_block_xor(acc, block);
			crypto_gcm_mul(acc, ctx->H);
		}
		memset(block, 0, GCM_BLOCK_SIZE);
		crypto_store_be64(((IUINT64)iv_len) << 3, block + 8);
		crypto_gcm_block_xor(acc, block);
		crypto_gcm_mul(acc, ctx->H);
		memcpy(ctx->J0, acc, GCM_BLOCK_SIZE);
	}
}

// Build length block for final authentication
static void crypto_gcm_build_length(CRYPTO_GCM_CTX *ctx, IUINT8 block[16]) {
	IUINT64 aad_bits = ctx->aad_len << 3;
	IUINT64 data_bits = ctx->data_len << 3;
	memset(block, 0, GCM_BLOCK_SIZE);
	crypto_store_be64(aad_bits, block);
	crypto_store_be64(data_bits, block + 8);
}

// Reset GCM state
static void crypto_gcm_reset_state(CRYPTO_GCM_CTX *ctx) {
	memset(ctx->auth, 0, GCM_BLOCK_SIZE);
	memset(ctx->aad_buf, 0, GCM_BLOCK_SIZE);
	memset(ctx->data_buf, 0, GCM_BLOCK_SIZE);
	memset(ctx->keystream, 0, GCM_BLOCK_SIZE);
	ctx->keystream_used = GCM_BLOCK_SIZE;
	ctx->aad_buf_len = 0;
	ctx->data_buf_len = 0;
	ctx->aad_len = 0;
	ctx->data_len = 0;
	ctx->aad_finalized = 0;
	ctx->data_started = 0;
}

// initialize AES-GCM context with key
void CRYPTO_GCM_Init(CRYPTO_GCM_CTX *ctx, const IUINT8 *key,
		IUINT32 keylen)
{
	IUINT8 zero[GCM_BLOCK_SIZE];
	IASSERT(ctx != NULL);
	IASSERT(key != NULL);
	memset(ctx, 0, sizeof(*ctx));
	CRYPTO_AES_Init(&ctx->aes, key, keylen);
	memset(zero, 0, sizeof(zero));
	CRYPTO_AES_Encrypt(&ctx->aes, ctx->H, zero);
	ctx->active = 0;
}

// reset AES-GCM context with iv
void CRYPTO_GCM_Reset(CRYPTO_GCM_CTX *ctx, const IUINT8 *iv,
		size_t iv_len)
{
	IASSERT(ctx != NULL);
	IASSERT(iv != NULL);
	IASSERT(iv_len > 0);
	crypto_gcm_reset_state(ctx);
	crypto_gcm_setup_j0(ctx, iv, iv_len);
	memcpy(ctx->counter, ctx->J0, GCM_BLOCK_SIZE);
	ctx->active = 1;
}

// update additional authenticated data (AAD)
void CRYPTO_GCM_UpdateAAD(CRYPTO_GCM_CTX *ctx, const void *aad,
		size_t aad_len)
{
	IASSERT(ctx != NULL);
	IASSERT(aad_len == 0 || aad != NULL);
	IASSERT(ctx->active != 0);
	IASSERT(ctx->data_started == 0);
	if (aad_len == 0) {
		return;
	}
	crypto_gcm_feed_bytes(ctx, (const IUINT8*)aad, aad_len, ctx->aad_buf,
		&ctx->aad_buf_len);
	ctx->aad_len += (IUINT64)aad_len;
}

// encrypt/decrypt bytes
static void crypto_gcm_encrypt_bytes(CRYPTO_GCM_CTX *ctx, IUINT8 *out,
		const IUINT8 *in, size_t len)
{
	size_t offset = 0;
	while (offset < len) {
		if (ctx->keystream_used == GCM_BLOCK_SIZE) {
			crypto_gcm_gen_keystream(ctx);
		}
		{
			size_t space = (size_t)GCM_BLOCK_SIZE - ctx->keystream_used;
			size_t step = (len - offset < space) ? (len - offset) : space;
			size_t i;
			for (i = 0; i < step; ++i) {
				out[offset + i] = in[offset + i] ^
					ctx->keystream[ctx->keystream_used + i];
			}
			ctx->keystream_used += (IUINT32)step;
			offset += step;
		}
	}
}

// encrypt data
void CRYPTO_GCM_Encrypt(CRYPTO_GCM_CTX *ctx, void *out,
		const void *in, size_t len)
{
	IASSERT(ctx != NULL);
	IASSERT(in != NULL || len == 0);
	IASSERT(out != NULL || len == 0);
	IASSERT(ctx->active != 0);
	if (len == 0) {
		return;
	}
	crypto_gcm_finalize_aad(ctx);
	ctx->data_started = 1;
	crypto_gcm_encrypt_bytes(ctx, (IUINT8*)out, (const IUINT8*)in, len);
	crypto_gcm_feed_bytes(ctx, (const IUINT8*)out, len, ctx->data_buf,
		&ctx->data_buf_len);
	ctx->data_len += (IUINT64)len;
}

// decrypt data
void CRYPTO_GCM_Decrypt(CRYPTO_GCM_CTX *ctx, void *out,
		const void *in, size_t len)
{
	IASSERT(ctx != NULL);
	IASSERT(in != NULL || len == 0);
	IASSERT(out != NULL || len == 0);
	IASSERT(ctx->active != 0);
	if (len == 0) {
		return;
	}
	crypto_gcm_finalize_aad(ctx);
	ctx->data_started = 1;
	crypto_gcm_feed_bytes(ctx, (const IUINT8*)in, len, ctx->data_buf,
		&ctx->data_buf_len);
	crypto_gcm_encrypt_bytes(ctx, (IUINT8*)out, (const IUINT8*)in, len);
	ctx->data_len += (IUINT64)len;
}

// finalize and get authentication tag
void CRYPTO_GCM_Final(CRYPTO_GCM_CTX *ctx, IUINT8 *tag,
		size_t tag_len)
{
	IUINT8 block[GCM_BLOCK_SIZE];
	IUINT8 S[GCM_BLOCK_SIZE];
	size_t copy_len;
	IASSERT(ctx != NULL);
	IASSERT(tag_len == 0 || tag != NULL);
	IASSERT(ctx->active != 0);
	crypto_gcm_finalize_aad(ctx);
	crypto_gcm_pad_buffer(ctx, ctx->data_buf, &ctx->data_buf_len);
	crypto_gcm_build_length(ctx, block);
	crypto_gcm_block_xor(ctx->auth, block);
	crypto_gcm_mul(ctx->auth, ctx->H);
	CRYPTO_AES_Encrypt(&ctx->aes, S, ctx->J0);
	crypto_gcm_block_xor(S, ctx->auth);
	copy_len = (tag_len > CRYPTO_GCM_TAG_SIZE) ?
		CRYPTO_GCM_TAG_SIZE : tag_len;
	if (tag != NULL && copy_len > 0) {
		memcpy(tag, S, copy_len);
	}
	ctx->active = 0;
}

// check authentication tag, return 0 if tag matches
int CRYPTO_GCM_CheckTag(CRYPTO_GCM_CTX *ctx, const IUINT8 *tag,
		size_t tag_len)
{
	IUINT8 expected[CRYPTO_GCM_TAG_SIZE];
	unsigned int diff = 0;
	size_t i;
	IASSERT(ctx != NULL);
	IASSERT(tag != NULL || tag_len == 0);
	CRYPTO_GCM_Final(ctx, expected, CRYPTO_GCM_TAG_SIZE);
	if (tag_len == 0 || tag_len > CRYPTO_GCM_TAG_SIZE) {
		return -1;
	}
	for (i = 0; i < CRYPTO_GCM_TAG_SIZE; ++i) {
		IUINT8 provided = 0;
		IUINT8 mask = 0;
		if (i < tag_len) {
			provided = tag[i];
			mask = 0xffu;
		}
		diff |= (unsigned int)((expected[i] ^ provided) & mask);
	}
	return (diff == 0) ? 0 : -1;
}



//=====================================================================
// CRYPTO Misc Functions
//=====================================================================

// xor mask with each byte
void CRYPTO_XOR_Byte(void *in, const void *out, int size, IUINT8 mask)
{
	const unsigned char *src = (const unsigned char*)in;
	unsigned char *dst = (unsigned char*)out;
	for (; size > 0; src++, dst++, size--) {
		dst[0] = src[0] ^ mask;
	}
}

// xor mask with each uint32
void CRYPTO_XOR_DWord(void *in, const void *out, int size, IUINT32 mask)
{
	const unsigned char *src = (const unsigned char*)in;
	unsigned char *dst = (unsigned char*)out;
	unsigned char cc[4];
	int i;
	cc[0] = (unsigned char)(mask & 0xff);
	cc[1] = (unsigned char)((mask >> 8) & 0xff);
	cc[2] = (unsigned char)((mask >> 16) & 0xff);
	cc[3] = (unsigned char)((mask >> 24) & 0xff);
	for (i = 0; i < size; i++) {
		dst[i] = src[i] ^ cc[i & 3];
	}
}

// xor string with each byte
void CRYPTO_XOR_String(void *in, const void *out, int size, 
		const unsigned char *mask, int msize, IUINT32 nonce)
{
	const unsigned char *src = (const unsigned char*)in;
	unsigned char *dst = (unsigned char*)out;
	// xor without nonce
	if (nonce == 0) {
		// check if msize is power of 2
		if ((msize & (msize - 1)) == 0) {
			int sizemask = msize - 1;
			int index = 0;
			for (; size > 0; src++, dst++, size--) {
				dst[0] = src[0] ^ mask[index & sizemask];
				index++;
			}
		}
		else {
			const unsigned char *mptr = mask;
			const unsigned char *mend = mask + msize;
			for (; size > 0; src++, dst++, size--) {
				dst[0] = src[0] ^ mptr[0];
				mptr++;
				if (mptr >= mend) {
					mptr = mask;
				}
			}
		}
	}
	else {  // with nonce
		unsigned char cc[4];
		int index = 0;
		cc[0] = (unsigned char)(nonce & 0xff);
		cc[1] = (unsigned char)((nonce >> 8) & 0xff);
		cc[2] = (unsigned char)((nonce >> 16) & 0xff);
		cc[3] = (unsigned char)((nonce >> 24) & 0xff);
		// check if msize is power of 2
		if ((msize & (msize - 1)) == 0) {
			int sizemask = msize - 1;
			for (; size > 0; src++, dst++, size--) {
				dst[0] = src[0] ^ (mask[index & sizemask] ^ cc[index & 3]);
				index++;
			}
		}
		else {
			const unsigned char *mptr = mask;
			const unsigned char *mend = mask + msize;
			for (; size > 0; src++, dst++, size--) {
				dst[0] = src[0] ^ (mptr[0] ^ cc[index & 3]);
				index++;
				mptr++;
				if (mptr >= mend) {
					mptr = mask;
				}
			}
		}
	}
}

// xor two buffers: out[i] = in1[i] ^ in2[i]
void CRYPTO_XOR_Combine(void *out, const void *in1, const void *in2, int size)
{
	const unsigned char *src1 = (const unsigned char*)in1;
	const unsigned char *src2 = (const unsigned char*)in2;
	unsigned char *dst = (unsigned char*)out;
	for (; size > 0; src1++, src2++, dst++, size--) {
		dst[0] = src1[0] ^ src2[0];
	}
}

// chain modes (seed acts as the "previous" byte for index 0):
// 0: xor chain forward, out[0] = in[0] ^ out[-1]
// 1: xor chain backward, out[0] = in[0] ^ in[-1]
// 2: add chain forward, out[0] = in[0] + out[-1]
// 3: add chain backward, out[0] = in[0] - in[-1]
void CRYPTO_XOR_Chain(void *out, const void *in, int size, IUINT8 *seed, int mode)
{
	const IUINT8 *src = (const IUINT8*)in;
	IUINT8 *dst = (IUINT8*)out;
	IUINT8 mask = (seed)? seed[0] : 0xaa;
	switch (mode) {
	case 0:   // xor chain forward
		for (; size > 0; src++, dst++, size--) {
			dst[0] = src[0] ^ mask;
			mask = dst[0];
		}
		break;
	case 1:   // xor chain backward
		for (; size > 0; src++, dst++, size--) {
			IUINT8 current = src[0];
			dst[0] = current ^ mask;
			mask = current;
		}
		break;
	case 2:   // add chain forward
		for (; size > 0; src++, dst++, size--) {
			dst[0] = src[0] + mask;
			mask = dst[0];
		}
		break;
	case 3:   // add chain backward
		for (; size > 0; src++, dst++, size--) {
			IUINT8 current = src[0];
			dst[0] = current - mask;
			mask = current;
		}
		break;

	}
	if (seed) seed[0] = mask;
}


//=====================================================================
// LCG: https://en.wikipedia.org/wiki/Linear_congruential_generator
//=====================================================================

// rand() in stdlib.h (c99), output range: 0 <= x <= 32767
IUINT32 random_std_c99(IUINT32 *seed) 
{
	seed[0] = seed[0] * 1103515245 + 12345;
	return (seed[0] >> 16) & 0x7fff;
}

// rand() in stdlib.h (msvc), output range: 0 <= x <= 32767
IUINT32 random_std_msvc(IUINT32 *seed) 
{
	seed[0] = seed[0] * 214013 + 2531011;
	return (seed[0] >> 16) & 0x7fff;
}

// minstd_rand in C++, output range: 0 <= x < 0x7fffffff
IUINT32 random_std_cpp(IUINT32 *seed) 
{
    const IUINT32 N = 0x7fffffff;
    const IUINT32 G = 48271u;
	IUINT32 div, rem, a, b;
	if (seed[0] == 0) seed[0] = 11;
    div = seed[0] / (N / G);  /* max : 2,147,483,646 / 44,488 = 48,271 */
    rem = seed[0] % (N / G);  /* max : 2,147,483,646 % 44,488 = 44,487 */
    a = rem * G;        /* max : 44,487 * 48,271 = 2,147,431,977 */
    b = div * (N % G);  /* max : 48,271 * 3,399 = 164,073,129 */
    seed[0] = (a > b) ? (a - b) : (a + (N - b));
	return seed[0];
}


//=====================================================================
// Statistically perfect random generator
//=====================================================================

void RANDOM_BOX_Init(RANDOM_BOX *box, IUINT32 *state, IUINT32 size)
{
	box->state = state;
	box->size = size;
	box->seed = 0;
	box->avail = 0;
	assert(size > 0);
}


// change seed
void RANDOM_BOX_Seed(RANDOM_BOX *box, IUINT32 seed)
{
	box->seed = seed;
}


// next random number within 0 <= x < size
IUINT32 RANDOM_BOX_Next(RANDOM_BOX *box)
{
	IUINT32 *state = box->state;
	IUINT32 x, y;
	if (box->avail == 0) {
		box->avail = box->size;
		for (x = 0; x < box->size; x++) 
			state[x] = x;	
	}
	x = random_std_cpp(&(box->seed)) % box->avail;
	y = state[x];
	box->avail--;
	state[x] = state[box->avail];
	return y;
}



//=====================================================================
// PCG: PCG is a family of simple fast statistically good algorithms
//=====================================================================

// static initializer
const RANDOM_PCG RANDOM_PCG_INITIALIZER = {
	0x853c49e6748fea9b, 0xda3e39cb94b95bdb
};

// initialize pcg 
void RANDOM_PCG_Init(RANDOM_PCG *pcg, IUINT64 initstate, IUINT64 initseq)
{
	pcg->state = 0;
	pcg->inc = (initseq << 1) | 1;
	RANDOM_PCG_Next(pcg);
	pcg->state += initstate;
	RANDOM_PCG_Next(pcg);
}


// next random number 
IUINT32 RANDOM_PCG_Next(RANDOM_PCG *pcg)
{
	const IUINT64 multiplier = 6364136223846793005u;
	IUINT64 state = pcg->state;
	IUINT32 xorshifted, rot, irot;
	pcg->state = state * multiplier + pcg->inc;
    xorshifted = (IUINT32)(((state >> 18) ^ state) >> 27);
    rot = (IUINT32)(state >> 59);
	irot = (0xffffffff - rot) + 1;
    return (xorshifted >> rot) | (xorshifted << (irot & 31));
}


// next random number within 0 <= x < bound
IUINT32 RANDOM_PCG_RANGE(RANDOM_PCG *pcg, IUINT32 bound)
{
	IUINT32 threshold, hr = 0;
	if (bound <= 1) return 0;
	threshold = (((0xffffffff - bound)) + 1) % bound;
	while (1) {
        IUINT32 r = RANDOM_PCG_Next(pcg);
        if (r >= threshold) {
            hr = r % bound;
			break;
		}
    }
	return hr;
}


