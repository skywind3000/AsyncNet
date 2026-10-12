//=====================================================================
//
// isecure.h - secure hash encrypt
//
// NOTE:
// for more information, please see the readme file.
//
// Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.
// Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
// Copyright 2008, Andrew Moon (curve25519-donna, public domain / MIT)
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
// Global Definition
//=====================================================================

// some old compilers do not support ULL suffix, so we use a macro.
#define CRYPTO_MAKE_QWORD(h, l) ((((IUINT64)(h)) << 32) | (IUINT64)(l))


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

//---------------------------------------------------------------------
// SHA-1 hardware acceleration callback
//---------------------------------------------------------------------
// single-block compression callback slot, same convention as the
// other isecure callbacks: install once at startup, never swap at
// runtime, default NULL = built-in implementation.
//
// contract: compress one 64-byte big-endian block into state,
// state[0..4] holds the standard SHA-1 chain words (big-endian
// values as plain u32), updated in place exactly like
// HASH_SHA1_Transform. covers Update/Final/HMAC automatically;
// the ctx is not required to be aligned (use unaligned loads).
// SHA-NI (x86 leaf 7 EBX bit 25) and ARMv8 CE SHA1 instructions
// consume this layout directly - no transform step needed, which
// is why (unlike GCM) there is a single callback per algorithm.
typedef void (*HASH_SHA1_COMPRESS_PROC)(IUINT32 state[5],
		const IUINT8 *block);

// callback slot, default NULL (built-in implementation)
extern HASH_SHA1_COMPRESS_PROC HASH_SHA1_COMPRESS_CB;


//=====================================================================
// SHA-256: Secure Hash Algorithm 256-bit (FIPS 180-4)
//=====================================================================
typedef struct {
    IUINT32 state[8];
    IUINT64 count;
    unsigned char buffer[64];
}   HASH_SHA256_CTX;

void HASH_SHA256_Init(HASH_SHA256_CTX *ctx);
void HASH_SHA256_Update(HASH_SHA256_CTX *ctx, const void *input, unsigned int len);
void HASH_SHA256_Final(HASH_SHA256_CTX *ctx, unsigned char digest[32]);

//---------------------------------------------------------------------
// SHA-256 hardware acceleration callback
//---------------------------------------------------------------------
// same install-once convention, default NULL = built-in software.
// contract: compress one 64-byte big-endian block into state,
// state[0..7] holds the standard SHA-256 chain words updated in
// place exactly like HASH_SHA256_Transform. covers Update/Final,
// HMAC-SHA256 and HKDF-SHA256 automatically.
typedef void (*HASH_SHA256_COMPRESS_PROC)(IUINT32 state[8],
		const IUINT8 *block);

// callback slot, default NULL (built-in implementation)
extern HASH_SHA256_COMPRESS_PROC HASH_SHA256_COMPRESS_CB;


//=====================================================================
// UTILITIES
//=====================================================================

// convert digests to string
char* hash_digest_to_string(char *out, const unsigned char *in, int size);

// convert hex string to digests, returns the number of bytes written to
// out (or the number of bytes that would be written if out is NULL, so it
// can be used to probe the required buffer size). leading whitespace and
// an optional "0x"/"0X"/"$" prefix are skipped, and conversion stops at
// the first non-hex character (trailing garbage is silently ignored).
// if size < 0 the input is treated as NUL-terminated, otherwise only the
// first size bytes are used, and out must hold at least size/2 bytes.
int hash_string_to_digest(unsigned char *out, const char *in, int size);

// calculate md5sum and convert digests to string
char* hash_md5sum(char *out, const void *in, unsigned int len);

// calculate sha1sum and convert digests to string
char* hash_sha1sum(char *out, const void *in, unsigned int len);

// calculate sha256sum and convert digests to string
char* hash_sha256sum(char *out, const void *in, unsigned int len);

// calculate crc32 and return result
IUINT32 hash_crc32(const void *in, unsigned int len);

// sum all bytes together
IUINT32 hash_checksum(const void *in, unsigned int len);

// calculate adler32 and return result
IUINT32 hash_adler32(const void *in, unsigned int len);

// calculate a hash value for a Lua string, used in Lua internal string table
IUINT32 hash_lua_string(const void *in, unsigned int len, unsigned int seed);


//=====================================================================
// HMAC functions
//=====================================================================

// calculate HMAC-MD5, digest must be 16 bytes buffer
void hash_hmac_md5(const void *msgs[], const int *sizes, int count, 
		const void *key, int keylen, void *digest);

// calculate HMAC-SHA1, digest must be 20 bytes buffer
void hash_hmac_sha1(const void *msgs[], const int *sizes, int count, 
		const void *key, int keylen, void *digest);

// calculate HMAC-SHA256, digest must be 32 bytes buffer
void hash_hmac_sha256(const void *msgs[], const int *sizes, int count, 
		const void *key, int keylen, void *digest);


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
// CRYPTO_OS_RANDOM - CSPRNG
//=====================================================================

// callback slot of CRYPTO_OS_RANDOM, can be replaced by user: since
// isecure.c is pure algorithm without any system calls, the real os
// random source (/dev/urandom, BCryptGenRandom, getrandom ...) should
// be installed by upper layers. Default is NULL, which means using
// the built-in fallback simulation (rand() based and whitened by
// splitmix64 style mixing, NOT cryptographically secure, mainly for
// simulation/tests).
extern int (*CRYPTO_OS_RANDOM_CB)(void *buf, size_t size);

// unified interface for os-random: call CRYPTO_OS_RANDOM_CB if it has
// been installed, otherwise call the built-in fallback simulation.
// returns zero for success, non-zero for failure (invalid arguments
// or the installed CRYPTO_OS_RANDOM_CB reports an error).
int CRYPTO_OS_RANDOM(void *buf, size_t size);


//=====================================================================
// Diffie-Hellman key exchange
// http://zh.wikipedia.org/wiki/Diffie%E2%80%93Hellman_key_exchange
// usage: 1. get an local asymmetric-key a from DH_Random
//        2. calculate A=(5 ^ a) % p by DH_Exchange
//        3. send A to remote
//        4. obtain symmetrical-key by DH_Key(local_key, RemoteA)
//=====================================================================

// returns random local key using rand() (call srand() first)
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

//---------------------------------------------------------------------
// chacha20 hardware acceleration callback
//---------------------------------------------------------------------
// bulk keystream generation callback slot, same convention as the
// other isecure callbacks: install once at startup, never swap at
// runtime, default NULL = built-in implementation.
//
// generates 'blocks' consecutive 64-byte keystream blocks starting
// at the counter in ctx->state[12], writing blocks*64 bytes to the
// caller-provided buffer out, then advances ctx->state[12] by
// 'blocks'. does NOT touch ctx->keystream / ctx->position (the
// caller manages consumption). state[16] is the standard chacha
// layout (constants || key || counter || nonce), so SIMD backends
// (AVX2 processes 8 blocks per pass) can load it directly. called
// from inside CRYPTO_CHACHA20_Update with a stack staging buffer,
// so both raw chacha20 and the ChaCha20-Poly1305 AEAD data path are
// covered automatically; the ctx is not required to be aligned.
typedef void (*CRYPTO_CHACHA20_BLOCKS_PROC)(CRYPTO_CHACHA20_CTX *ctx,
		IUINT8 *out, size_t blocks);

// callback slot, default NULL (built-in implementation)
extern CRYPTO_CHACHA20_BLOCKS_PROC CRYPTO_CHACHA20_BLOCKS_CB;



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

// keylen: 16, 24, 32 bytes. returns 0 on success, -1 on invalid
// arguments (NULL key, or keylen other than 16/24/32 -- non-standard
// lengths are rejected, NOT silently extended or truncated; on error
// the context is zeroed and must be re-initialized before use)
int CRYPTO_AES_Init(CRYPTO_AES_CTX *ctx, const IUINT8 *key, IUINT32 keylen);

// encrypt a single AES block (16 bytes)
void CRYPTO_AES_Encrypt(CRYPTO_AES_CTX *ctx, IUINT8 *out, const IUINT8 *in);

// decrypt a single AES block (16 bytes)
void CRYPTO_AES_Decrypt(CRYPTO_AES_CTX *ctx, IUINT8 *out, const IUINT8 *in);


//---------------------------------------------------------------------
// AES/GCM hardware acceleration callbacks
//---------------------------------------------------------------------
// single-block encryption callback slot, same convention as
// CRYPTO_X25519_SCALARMULT_CB: install once at startup, never swap
// at runtime, read lock-free. when NULL (default) the built-in
// software implementation above is used and behavior is unchanged.
//
// ctx contract: the callback receives a context prepared by the
// built-in CRYPTO_AES_Init and MUST consume the standard round-key
// layout, i.e. key_enc[] as 'rounds+1' groups of 4 u32 (rounds =
// 6 + key_length/4, 10/12/14) stored as little-endian words. on
// little-endian machines (all x86) the in-memory byte order of each
// group equals the FIPS-197 round key, so hardware AES engines
// (x86 AES-NI, ARMv8 CE) can load it directly. the ctx is NOT
// guaranteed to be 16-byte aligned: use unaligned loads. ctx->
// key_length must keep the raw key size in bytes. out and in may
// alias (in is fully consumed before out is written).
typedef void (*CRYPTO_AES_CRYPT_PROC)(CRYPTO_AES_CTX *ctx,
		IUINT8 *out, const IUINT8 *in);

// callback slot, default NULL (built-in implementation)
extern CRYPTO_AES_CRYPT_PROC CRYPTO_AES_ENCRYPT_CB;

// GF(2^128) hash-key transform callback for AES-GCM: invoked once
// inside CRYPTO_GCM_Init right after H = E_K(0) is computed. it may
// rewrite the 16 bytes of H in place with an implementation-private
// representation (e.g. bit- or byte-reordered for CLMUL). MUST be
// installed together with CRYPTO_GCM_MUL_CB: the built-in bit-serial
// multiplication can no longer interpret a transformed H. CRYPTO_GCM_
// Reset reuses the transformed H as-is, so no re-transform happens.
typedef void (*CRYPTO_GCM_HASHKEY_PROC)(IUINT8 H[16]);

// GF(2^128) multiplication callback for AES-GCM: X = X * H in place,
// where H is the (possibly transformed) value of ctx->H. covers all
// internal call sites of the built-in crypto_gcm_mul(), including
// GHASH chaining, non-96-bit-IV J0 derivation and the final tag.
typedef void (*CRYPTO_GCM_MUL_PROC)(IUINT8 X[16], const IUINT8 H[16]);

extern CRYPTO_GCM_HASHKEY_PROC CRYPTO_GCM_HASHKEY_CB;
extern CRYPTO_GCM_MUL_PROC CRYPTO_GCM_MUL_CB;


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

// initialize AES-GCM context with key (keylen: 16/24/32 bytes),
// returns 0 on success, -1 on invalid arguments
int CRYPTO_GCM_Init(CRYPTO_GCM_CTX *ctx, const IUINT8 *key,
		IUINT32 keylen);

// reset AES-GCM context with iv (any length > 0), returns 0 on
// success, -1 on invalid arguments (NULL iv or iv_len == 0) or an
// uninitialized key (Reset after a failed Init is refused)
int CRYPTO_GCM_Reset(CRYPTO_GCM_CTX *ctx, const IUINT8 *iv,
		size_t iv_len);

// update additional authenticated data (AAD), must precede
// Encrypt/Decrypt, returns 0 on success, -1 on misuse (inactive ctx
// or data already started)
int CRYPTO_GCM_UpdateAAD(CRYPTO_GCM_CTX *ctx, const void *aad,
		size_t aad_len);

// encrypt data, returns 0 on success, -1 on misuse or bad arguments
int CRYPTO_GCM_Encrypt(CRYPTO_GCM_CTX *ctx, void *out,
		const void *in, size_t len);

// decrypt data, returns 0 on success, -1 on misuse or bad arguments
int CRYPTO_GCM_Decrypt(CRYPTO_GCM_CTX *ctx, void *out,
		const void *in, size_t len);

// finalize and get authentication tag (tag_len <= 16), returns 0 on
// success, -1 if tag_len > 16 or the ctx is inactive (Final/CheckTag
// already ran and consumed the message state)
int CRYPTO_GCM_Final(CRYPTO_GCM_CTX *ctx, IUINT8 *tag,
		size_t tag_len);

// check authentication tag, return 0 if tag matches, -1 on mismatch,
// invalid tag_len (0 or > 16) or inactive ctx
int CRYPTO_GCM_CheckTag(CRYPTO_GCM_CTX *ctx, const IUINT8 *tag,
		size_t tag_len);


//=====================================================================
// CRYPTO_POLY1305: ChaCha20-Poly1305 AEAD (RFC 8439), stream-based
// authenticated encryption, same usage model as CRYPTO_GCM_* above
//=====================================================================
typedef struct {
	CRYPTO_CHACHA20_CTX chacha;	// keystream generator (counter from 1)
	IUINT8 key[32];				// saved chacha20 key (32 bytes)
	IUINT32 poly_r[5];			// poly1305 r (clamped, 26-bit limbs)
	IUINT32 poly_h[5];			// poly1305 accumulator
	IUINT32 poly_pad[4];		// poly1305 s (one-time key part)
	IUINT8 aad_buf[16];			// AAD staging block
	IUINT8 data_buf[16];		// ciphertext staging block
	IUINT32 aad_buf_len;
	IUINT32 data_buf_len;
	IUINT64 aad_len;
	IUINT64 data_len;
	int aad_finalized;
	int data_started;
	int active;
}	CRYPTO_POLY1305_CTX;

#define CRYPTO_POLY1305_TAG_SIZE 16

// initialize context with key (32 bytes), returns 0 on success,
// -1 on NULL key
int CRYPTO_POLY1305_Init(CRYPTO_POLY1305_CTX *ctx, const IUINT8 *key);

// reset context with nonce (12 bytes) and start a new message:
// derives the poly1305 one-time key from chacha20 block 0,
// returns 0 on success, -1 on NULL nonce
int CRYPTO_POLY1305_Reset(CRYPTO_POLY1305_CTX *ctx, const IUINT8 *nonce);

// update additional authenticated data (AAD), must precede
// Encrypt/Decrypt, returns 0 on success, -1 on misuse (inactive ctx
// or data already started)
int CRYPTO_POLY1305_UpdateAAD(CRYPTO_POLY1305_CTX *ctx, const void *aad,
		size_t aad_len);

// encrypt data (out may alias in), tag is computed over the
// ciphertext, returns 0 on success, -1 on misuse or bad arguments
int CRYPTO_POLY1305_Encrypt(CRYPTO_POLY1305_CTX *ctx, void *out,
		const void *in, size_t len);

// decrypt data (out may alias in), returns 0 on success, -1 on
// misuse or bad arguments
int CRYPTO_POLY1305_Decrypt(CRYPTO_POLY1305_CTX *ctx, void *out,
		const void *in, size_t len);

// finalize and get authentication tag (tag_len <= 16), returns 0 on
// success, -1 if tag_len > 16 or the ctx is inactive
int CRYPTO_POLY1305_Final(CRYPTO_POLY1305_CTX *ctx, IUINT8 *tag,
		size_t tag_len);

// check authentication tag, return 0 if tag matches, -1 on mismatch,
// invalid tag_len (0 or > 16) or inactive ctx
int CRYPTO_POLY1305_CheckTag(CRYPTO_POLY1305_CTX *ctx, const IUINT8 *tag,
		size_t tag_len);

//---------------------------------------------------------------------
// poly1305 hardware acceleration callback
//---------------------------------------------------------------------
// bulk block-absorption callback slot, same install-once convention
// as the other callbacks, default NULL = built-in implementation.
//
// absorbs nblocks full 16-byte message blocks (zero-padded to block
// multiples by the caller, exactly like the built-in feed) into the
// accumulator: per block, h = (h + m_i + 2^128) * r mod 2^130-5.
// reads ctx->poly_r and reads/writes ctx->poly_h, both in the
// canonical 26-bit limb format, and leaves h in the same partially
// reduced form as the built-in crypto_poly1305_block, so the
// built-in mac_finish keeps working and software/backend blocks can
// interleave within one message. the ctx is not required to be
// aligned. a bulk call lets a SIMD backend amortize its key-rotation
// vectors over many blocks; nblocks == 1 may take a scalar fallback.
typedef void (*CRYPTO_POLY1305_BLOCK_PROC)(CRYPTO_POLY1305_CTX *ctx,
		const IUINT8 *m, size_t nblocks);

// callback slot, default NULL (built-in implementation)
extern CRYPTO_POLY1305_BLOCK_PROC CRYPTO_POLY1305_BLOCK_CB;


//=====================================================================
// CRYPTO Misc Functions
//=====================================================================

// xor mask with each byte
void CRYPTO_XOR_Byte(void *out, const void *in, int size, IUINT8 mask);

// xor mask with each uint32
void CRYPTO_XOR_DWord(void *out, const void *in, int size, IUINT32 mask);

// xor string with each byte
void CRYPTO_XOR_String(void *out, const void *in, int size, 
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

// fill buffer with random bytes
void RANDOM_PCG_Fill(RANDOM_PCG *pcg, void *buf, size_t size);


//=====================================================================
// HMAC Signature with time
//=====================================================================

// generate a 40/48/72 bytes signature
char *hmac_signature(
		int method,            // 0/md5, 1/sha1, 2/sha256
		char *out,             // output string with size above 80 bytes
		const void *in,        // input data
		int size,              // input size
		const char *secret,    // secret token
		int secret_size,       // secret size
		IUINT32 timestamp);    // time stamp in unix epoch seconds

// extract timestamp from signature
IUINT32 hmac_signature_time(const char *signature);


//=====================================================================
// CRYPTO X25519: Elliptic Curve Diffie-Hellman (Curve25519, RFC 7748)
//=====================================================================

#define CRYPTO_X25519_KEY_SIZE    32      // size of key/secret (bytes)

/**
 * replaceable scalar multiplication callback (curve25519_donna
 * semantics): out = clamp(secret) * point, the callback MUST apply
 * RFC 7748 clamping (e[0]&=0xf8, e[31]&=0x7f, e[31]|=0x40) itself.
 * same convention as CRYPTO_OS_RANDOM_CB: install once at startup,
 * never swap at runtime, isecure reads it without locking.
 * default NULL = built-in portable pure C implementation.
 */
typedef void (*CRYPTO_X25519_SCALARMULT_PROC)(unsigned char out[32],
		const unsigned char secret[32], const unsigned char point[32]);

// callback slot, default NULL (built-in implementation)
extern CRYPTO_X25519_SCALARMULT_PROC CRYPTO_X25519_SCALARMULT_CB;

/**
 * generate a random private key (32 bytes) via CRYPTO_OS_RANDOM.
 * WARNING: when CRYPTO_OS_RANDOM_CB is not installed, the built-in
 * rand()-based fallback is used and the private key is predictable
 * (NOT cryptographically secure) -- install a real OS CSPRNG before
 * production use. always returns 0.
 */
int CRYPTO_X25519_Keygen(unsigned char private_key[CRYPTO_X25519_KEY_SIZE]);

/**
 * derive the public key: public_key = private_key * basepoint(9).
 * pure computation, never fails.
 */
void CRYPTO_X25519_Public(unsigned char public_key[CRYPTO_X25519_KEY_SIZE],
		const unsigned char private_key[CRYPTO_X25519_KEY_SIZE]);

/**
 * derive the shared secret: secret = private_key * peer_public.
 * returns 0 on success, -1 when the output is all-zero (low order
 * point, RFC 7748 recommended check, NOT a complete small-subgroup
 * detection): the caller MUST discard the session and never use
 * that key.
 */
int CRYPTO_X25519_Shared(unsigned char secret[CRYPTO_X25519_KEY_SIZE],
		const unsigned char private_key[CRYPTO_X25519_KEY_SIZE],
		const unsigned char peer_public[CRYPTO_X25519_KEY_SIZE]);


//=====================================================================
// Utility: helpful functions
//=====================================================================

// splitmix64 style mixing, used in PCG and other RNGs, also useful
// for whitening low-quality entropy sources.
static inline IUINT64 CRYPTO_SplitMix64(IUINT64 x) {
	x ^= x >> 30;
	x *= CRYPTO_MAKE_QWORD(0xbf58476d, 0x1ce4e5b9);
	x ^= x >> 27;
	x *= CRYPTO_MAKE_QWORD(0x94d049bb, 0x133111eb);
	x ^= x >> 31;
	return x;
}

// splitmix64 style RNG: state is updated, returns next random number
static inline IUINT64 CRYPTO_SplitMix64_Next(IUINT64 *state) {
	IUINT64 x = *state;
	*state = x + CRYPTO_MAKE_QWORD(0x9e3779b9, 0x7f4a7c15);
	return CRYPTO_SplitMix64(x);
}

// xorshift32: simple fast statistically good RNG, 
// NOT cryptographically secure
static inline IUINT32 CRYPTO_XorShift32(IUINT32 x) {
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

// xorshift64: simple fast statistically good RNG,
// NOT cryptographically secure
static inline IUINT64 CRYPTO_XorShift64(IUINT64 x) {
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}


//=====================================================================
// CRYPTO HKDF: HMAC-SHA256 based key derivation (RFC 5869)
//=====================================================================

// hash output / PRK size in bytes
#define CRYPTO_HKDF_SHA256_SIZE 32

// maximum output length in bytes (255 hash blocks per RFC 5869)
#define CRYPTO_HKDF_SHA256_MAX (255 * 32)

// Extract step: PRK = HMAC-SHA256(salt, IKM). salt may be NULL or
// empty, which is replaced by 32 zero bytes as required by RFC 5869.
// returns 0 on success, -1 on invalid arguments
int CRYPTO_HKDF_SHA256_Extract(IUINT8 prk[CRYPTO_HKDF_SHA256_SIZE],
		const void *salt, int salt_len, const void *ikm, int ikm_len);

// Expand step: OKM = HKDF-Expand(PRK, info, okm_len), returns the
// derived output key material. returns 0 on success, -1 if okm_len
// is 0 or above CRYPTO_HKDF_SHA256_MAX, or the info pointer and
// length disagree
int CRYPTO_HKDF_SHA256_Expand(void *okm, size_t okm_len,
		const IUINT8 prk[CRYPTO_HKDF_SHA256_SIZE],
		const void *info, int info_len);

// one-shot convenience: OKM = HKDF(IKM, salt, info, okm_len), i.e.
// Extract followed by Expand. returns 0 on success, -1 otherwise
int CRYPTO_HKDF_SHA256_Derive(void *okm, size_t okm_len,
		const void *ikm, int ikm_len, const void *salt, int salt_len,
		const void *info, int info_len);


//=====================================================================
// Utilities
//=====================================================================

// fast NON-cryptographic key length adapter
int CRYPTO_KeyStretch(void *out, int outlen, const void *in, int inlen);




#ifdef __cplusplus
}
#endif

#endif


