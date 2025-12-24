//=====================================================================
//
// isecure.h - secure hash encrypt
//
// NOTE:
// for more information, please see the readme file.
//
// Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.
// Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
//
//=====================================================================
#ifndef __ISECURE_H__
#define __ISECURE_H__

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>


//=====================================================================
// 32BIT INTEGER DEFINITION 
//=====================================================================
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(__UINT32_TYPE__) && defined(__INT32_TYPE__)
	typedef __UINT32_TYPE__ ISTDUINT32;
	typedef __INT32_TYPE__ ISTDINT32;
#elif defined(__UINT_FAST32_TYPE__) && defined(__INT_FAST32_TYPE__)
	typedef __UINT_FAST32_TYPE__ ISTDUINT32;
	typedef __INT_FAST32_TYPE__ ISTDINT32;
#elif defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__) && (__GNUC__ > 3)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else 
#include <limits.h>
#if ULONG_MAX == 0xFFFFU
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#else
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#endif
#endif
#endif


//=====================================================================
// Global Macros
//=====================================================================
#ifndef __IINT8_DEFINED
#define __IINT8_DEFINED
typedef signed char IINT8;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char IUINT8;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short IUINT16;
#endif

#ifndef __IINT16_DEFINED
#define __IINT16_DEFINED
typedef short IINT16;
#endif

#ifndef __IINT32_DEFINED
#define __IINT32_DEFINED
typedef ISTDINT32 IINT32;
#endif

#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef ISTDUINT32 IUINT32;
#endif

#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long IINT64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long IUINT64;
#endif
#endif

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

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif


//=====================================================================
// DETECTION WORD ORDER
//=====================================================================
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MISPEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif

#ifndef IASSERT
#define IASSERT(x) assert(x)
#endif


#ifdef __cplusplus
extern "C" {
#endif

//=====================================================================
// Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.
//=====================================================================
typedef struct {
	IUINT32 i[2];             // Number of _bits_ handled mod 2^64
	IUINT32 buf[4];           // Scratch buffer
	unsigned char in[64];     // Input buffer
}	HASH_MD5_CTX;

void HASH_MD5_Init(HASH_MD5_CTX *ctx, unsigned long RandomNumber);
void HASH_MD5_Update(HASH_MD5_CTX *ctx, const void *input, unsigned int len);
void HASH_MD5_Final(HASH_MD5_CTX *ctx, unsigned char digest[16]);



//=====================================================================
// From http://www.mirrors.wiretapped.net/security/cryptography
//=====================================================================
typedef struct {
	IUINT32 state[5];
    IUINT32 count[2];
    unsigned char buffer[64];
}	HASH_SHA1_CTX;

void HASH_SHA1_Init(HASH_SHA1_CTX *ctx);
void HASH_SHA1_Update(HASH_SHA1_CTX *ctx, const void *input, unsigned int len);
void HASH_SHA1_Final(HASH_SHA1_CTX *ctx, unsigned char digest[20]);


//=====================================================================
// UTILITIES
//=====================================================================

// convert digests to string
char* hash_digest_to_string(char *out, const unsigned char *in, int size);

// calculate md5sum and convert digests to string
char* hash_md5sum(char *out, const void *in, unsigned int len);

// calculate sha1sum and convert digests to string
char* hash_sha1sum(char *out, const void *in, unsigned int len);

// calculate crc32 and return result
IUINT32 hash_crc32(const void *in, unsigned int len);

// sum all bytes together
IUINT32 hash_checksum(const void *in, unsigned int len);


//=====================================================================
// Incremental hash update functions
//=====================================================================

// 32 bits fnv1a hash update
static inline IUINT32 hash_update_fnv1a(IUINT32 h, IUINT32 x) {
	const IUINT32 FNV1A_32_PRIME = 0x01000193;
	h = (h ^ x) * FNV1A_32_PRIME;
	return h;
}

// 32 bits boost hash update
static inline IUINT32 hash_update_boost(IUINT32 h, IUINT32 x) {
	h ^= x + 0x9e3779b9 + (h << 6) + (h >> 2);
	return h;
}

// 32 bits xxhash update
static inline IUINT32 hash_update_xxhash(IUINT32 h, IUINT32 x) {
	const IUINT32 PRIME32_2 = 0x85ebca77;
	const IUINT32 PRIME32_3 = 0xc2b2ae3d;
	h = h + x * PRIME32_2;
	h = ((h << 13) | (h >> 19)) * PRIME32_3;
	return h;
}

// 32 bits murmur hash update
static inline IUINT32 hash_update_murmur(IUINT32 h, IUINT32 x) {
	x = x * 0xcc9e2d51;
	x = ((x << 15) | (x >> 17));
	h = (h * 0x1b873593) ^ x;
	h = (h << 13) | (h >> 19);
	h = h * 5 + 0xe6546b64;
	return h;
}


//=====================================================================
// Diffie-Hellman key exchange
// http://zh.wikipedia.org/wiki/Diffie%E2%80%93Hellman_key_exchange
// usage: 1. get an local asymmetric-key a from DH_Random
//        2. calculate A=(5 ^ a) % p by DH_Exchange 
//        3. send A to remote
//        4. obtain symmetrical-key by DH_Key(local_key, RemoteA)
//=====================================================================

// returns random local key
IUINT64 DH_Random();

// calculate A/B which will be sent to remote
IUINT64 DH_Exchange(IUINT64 local);

// get final symmetrical-key from local key and remote A/B
IUINT64 DH_Key(IUINT64 local, IUINT64 remote);

// get qword from hex string 
void DH_STR_TO_U64(const char *str, IUINT64 *x);

// hex string from qword, capacity of str must above 17
void DH_U64_TO_STR(IUINT64 x, char *str);


//=====================================================================
// CRYPTO RC4
//=====================================================================
typedef struct {
	int x;
	int y;
	unsigned char box[256];
}	CRYPTO_RC4_CTX;


// Initialize RC4 context with key
void CRYPTO_RC4_Init(CRYPTO_RC4_CTX *ctx, const void *key, int keylen);

// Apply RC4 to data
void CRYPTO_RC4_Update(CRYPTO_RC4_CTX *ctx, void *out, const void *in, 
		size_t size);

// Apply RC4 to data (in-place)
void CRYPTO_RC4_Direct(const void *key, int keylen, void *out, const void *in,
		size_t size, int ntimes);



//=====================================================================
// CRYPTO chacha20: 
//=====================================================================
typedef struct {
	IUINT32 state[16];
	IUINT8 keystream[64];
	size_t position;
}	CRYPTO_CHACHA20_CTX;


// key: 32 bytes, nonce: 12 bytes
void CRYPTO_CHACHA20_Init(CRYPTO_CHACHA20_CTX *ctx, 
		const IUINT8 *key, const IUINT8 *nonce, IUINT32 counter);

// applay cipher
void CRYPTO_CHACHA20_Update(CRYPTO_CHACHA20_CTX *ctx, void *out,
		const void *in, size_t size);



//=====================================================================
// CRYPTO XTEA: https://en.wikipedia.org/wiki/XTEA
//=====================================================================

void CRYPTO_XTEA_Encipher(int nrounds, const IUINT32 key[4], IUINT32 v[2]);

void CRYPTO_XTEA_Decipher(int nrounds, const IUINT32 key[4], IUINT32 v[2]);



//=====================================================================
// AES: Advanced Encryption Standard (block-wise encrypt/decrypt)
//=====================================================================
typedef struct {
	IUINT32 key_enc[60];
	IUINT32 key_dec[60];
	IUINT32 key_length;
}   CRYPTO_AES_CTX;

// keylen: 16, 24, 32 bytes
void CRYPTO_AES_Init(CRYPTO_AES_CTX *ctx, const IUINT8 *key, IUINT32 keylen);

// encrypt a single AES block (16 bytes)
void CRYPTO_AES_Encrypt(CRYPTO_AES_CTX *ctx, IUINT8 *out, const IUINT8 *in);

// decrypt a single AES block (16 bytes)
void CRYPTO_AES_Decrypt(CRYPTO_AES_CTX *ctx, IUINT8 *out, const IUINT8 *in);


//=====================================================================
// AES-GCM: stream-based authenticated encryption
//=====================================================================
typedef struct {
	CRYPTO_AES_CTX aes;
	IUINT8 H[16];
	IUINT8 J0[16];
	IUINT8 counter[16];
	IUINT8 auth[16];
	IUINT8 aad_buf[16];
	IUINT8 data_buf[16];
	IUINT8 keystream[16];
	IUINT32 keystream_used;
	IUINT32 aad_buf_len;
	IUINT32 data_buf_len;
	IUINT64 aad_len;
	IUINT64 data_len;
	int aad_finalized;
	int data_started;
	int active;
}   CRYPTO_GCM_CTX;

#define CRYPTO_GCM_TAG_SIZE 16

// initialize AES-GCM context with key
void CRYPTO_GCM_Init(CRYPTO_GCM_CTX *ctx, const IUINT8 *key,
		IUINT32 keylen);

// reset AES-GCM context with iv
void CRYPTO_GCM_Reset(CRYPTO_GCM_CTX *ctx, const IUINT8 *iv,
		size_t iv_len);

// update additional authenticated data (AAD)
void CRYPTO_GCM_UpdateAAD(CRYPTO_GCM_CTX *ctx, const void *aad,
		size_t aad_len);

// encrypt data
void CRYPTO_GCM_Encrypt(CRYPTO_GCM_CTX *ctx, void *out,
		const void *in, size_t len);

// decrypt data
void CRYPTO_GCM_Decrypt(CRYPTO_GCM_CTX *ctx, void *out,
		const void *in, size_t len);

// finalize and get authentication tag
void CRYPTO_GCM_Final(CRYPTO_GCM_CTX *ctx, IUINT8 *tag,
		size_t tag_len);

// check authentication tag, return 0 if tag matches
int CRYPTO_GCM_CheckTag(CRYPTO_GCM_CTX *ctx, const IUINT8 *tag,
		size_t tag_len);


//=====================================================================
// CRYPTO Misc Functions
//=====================================================================

// xor mask with each byte
void CRYPTO_XOR_Byte(void *in, const void *out, int size, IUINT8 mask);

// xor mask with each uint32
void CRYPTO_XOR_DWord(void *in, const void *out, int size, IUINT32 mask);

// xor string with each byte
void CRYPTO_XOR_String(void *in, const void *out, int size, 
		const unsigned char *mask, int msize, IUINT32 nonce);

// xor two buffers: out[i] = in1[i] ^ in2[i]
void CRYPTO_XOR_Combine(void *out, const void *in1, const void *in2, int size);

// chain modes (seed acts as the "previous" byte for index 0):
// 0: xor chain forward, out[0] = in[0] ^ out[-1]
// 1: xor chain backward, out[0] = in[0] ^ in[-1]
// 2: add chain forward, out[0] = in[0] + out[-1]
// 3: add chain backward, out[0] = in[0] - in[-1]
void CRYPTO_XOR_Chain(void *out, const void *in, int size, IUINT8 *seed, int mode);


//=====================================================================
// LCG: https://en.wikipedia.org/wiki/Linear_congruential_generator
//=====================================================================

// rand() in stdlib.h (c99), output range: 0 <= x <= 32767
IUINT32 random_std_c99(IUINT32 *seed);

// rand() in stdlib.h (msvc), output range: 0 <= x <= 32767
IUINT32 random_std_msvc(IUINT32 *seed);

// minstd_rand in C++, output range: 0 <= x < 0x7fffffff
IUINT32 random_std_cpp(IUINT32 *seed);


//=====================================================================
// Statistically perfect random generator
//=====================================================================
typedef struct {
	IUINT32 seed;      // random seed
	IUINT32 size;      // array size
	IUINT32 avail;     // available numbers
	IUINT32 *state;    // states array
}	RANDOM_BOX;


// initialize random box
void RANDOM_BOX_Init(RANDOM_BOX *box, IUINT32 *state, IUINT32 size);

// change seed
void RANDOM_BOX_Seed(RANDOM_BOX *box, IUINT32 seed);

// next random number within 0 <= x < size
IUINT32 RANDOM_BOX_Next(RANDOM_BOX *box);


//=====================================================================
// PCG: PCG is a family of simple fast statistically good algorithms
//=====================================================================
typedef struct {
	IUINT64 state;    // RNG state.  All values are possible.
	IUINT64 inc;      // Must *always* be odd.
}	RANDOM_PCG;

// initialize pcg 
void RANDOM_PCG_Init(RANDOM_PCG *pcg, IUINT64 initstate, IUINT64 initseq);

// next random number 
IUINT32 RANDOM_PCG_Next(RANDOM_PCG *pcg);

// next random number within 0 <= x < bound
IUINT32 RANDOM_PCG_RANGE(RANDOM_PCG *pcg, IUINT32 bound);





#ifdef __cplusplus
}
#endif

#endif


