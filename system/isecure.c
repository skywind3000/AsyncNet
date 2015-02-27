//=====================================================================
//
// isecure.c - secure hash encrypt
//
// NOTE:
// for more information, please see the readme file.
//
// Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.
//
//=====================================================================
#include "isecure.h"
#include <stdlib.h>

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
static inline char *is_encode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *is_decode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *is_encode16u_lsb(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	*(unsigned short*)(p) = w;
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char *is_decode16u_lsb(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	*w = *(const unsigned short*)p;
#endif
	p += 2;
	return p;
}

/* encode 16 bits unsigned int (msb) */
static inline char *is_encode16u_msb(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned short*)(p) = w;
#else
	*(unsigned char*)(p + 0) = (w >> 8);
	*(unsigned char*)(p + 1) = (w & 255);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (msb) */
static inline const char *is_decode16u_msb(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN
	*w = *(const unsigned short*)p;
#else
	*w = *(const unsigned char*)(p + 0);
	*w = *(const unsigned char*)(p + 1) + (*w << 8);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *is_encode32u_lsb(char *p, unsigned long l)
{
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *is_decode32u_lsb(const char *p, unsigned long *l)
{
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
	p += 4;
	return p;
}

/* encode 32 bits unsigned int (msb) */
static inline char *is_encode32u_msb(char *p, unsigned long l)
{
	*(unsigned char*)(p + 0) = (unsigned char)((l >> 24) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >>  0) & 0xff);
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (msb) */
static inline const char *is_decode32u_msb(const char *p, unsigned long *l)
{
	*l = *(const unsigned char*)(p + 0);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 3) + (*l << 8);
	p += 4;
	return p;
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
void HASH_SHA1_Transform(IUINT32 state[5], const unsigned char buffer[64])
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
            HASH_SHA1_Transform(ctx->state, &data[i]);
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
char* hash_digest_to_string(const unsigned char *in, int size, char *out)
{
	static const char hex[17] = "0123456789abcdef";
	char *ptr = out;
	for (; 0 < size; size--) {
		unsigned char ch = *in++;
		*ptr++ = hex[ch >> 4];
		*ptr++ = hex[ch & 15];
	}
	*ptr++ = 0;
	return out;
}

// calculate md5sum and convert digests to string
char* hash_md5sum(const void *in, size_t len, char *out)
{
	static char text[48];
	unsigned char digest[16];
	HASH_MD5_CTX ctx;
	HASH_MD5_Init(&ctx, 0);
	HASH_MD5_Update(&ctx, in, len);
	HASH_MD5_Final(&ctx, digest);
	if (out == NULL) out = text;
	return hash_digest_to_string(digest, 16, out);
}

// calculate sha1sum and convert digests to string
char* hash_sha1sum(const void *in, size_t len, char *out)
{
	static char text[48];
	unsigned char digest[20];
	HASH_SHA1_CTX ctx;
	HASH_SHA1_Init(&ctx);
	HASH_SHA1_Update(&ctx, in, len);
	HASH_SHA1_Final(&ctx, digest);
	if (out == NULL) out = text;
	return hash_digest_to_string(digest, 20, out);
}

// crc32

/* Need an unsigned type capable of holding 32 bits; */
#define UPDC32(octet, crc) (crc_32_tab[((crc) ^ (octet)) & 0xff] ^ ((crc) >> 8))

IUINT32 crc_32_tab[] = { /* CRC polynomial 0xedb88320 */
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
IUINT32 hash_crc32(const void *in, size_t len)
{
	unsigned char* p = (unsigned char *)in;
	IUINT32 result = 0xffffffff;
	size_t i = 0;
	for (i = 0; i < len; i++){
		result = UPDC32(p[i], result);
	}
	result ^= 0xffffffff;  
	return result;
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
IUINT64 DH_Final(IUINT64 local, IUINT64 remote)
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
		a = (a << 16) | b;
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

void CRYPTO_RC4_Apply(CRYPTO_RC4_CTX *ctx, const void *in, void *out, 
	size_t size)
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


void CRYPTO_RC4_Crypto(const void *key, int keylen, const void *in,
	void *out, size_t size, int ntimes)
{
	const void *src = in;
	CRYPTO_RC4_CTX ctx;
	CRYPTO_RC4_Init(&ctx, key, keylen);
	for (; ntimes > 0; ntimes--) {
		CRYPTO_RC4_Apply(&ctx, src, out, size);
		src = out;
	}
}


