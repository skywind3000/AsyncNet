/*
 * plugin_secure.c - multi-platform hardware backend plugin for isecure
 *
 * fills the isecure callback slots (CRYPTO_AES_ENCRYPT_CB,
 * CRYPTO_GCM_HASHKEY_CB, CRYPTO_GCM_MUL_CB, see isecure.h) with the
 * fastest implementation this build supports:
 *
 *   x86 / x64  : AES-NI (single-block encryption) + PCLMULQDQ (GHASH)
 *                + AVX2 (chacha20 bulk keystream, poly1305 blocks)
 *                + SHA-NI (sha1/sha256 compression, leaf 7 EBX bit 25)
 *   aarch64    : Crypto Extensions (AESE/AESMC + PMULL + VRBIT)
 *   armv7 etc. : no backend (armv7 has no AES/PMULL instructions),
 *                Install returns -2 and the software path stays on
 *
 * the two CPU feature groups are detected independently: a Sandy
 * Bridge (AES-NI, no AVX2) installs only the AES/GCM callbacks, a
 * hypothetical AVX2-without-AES CPU installs only the chacha/poly
 * callbacks. AVX2 additionally requires OSXSAVE + XCR0 YMM state
 * (the YMM registers are only safe when the OS saves them).
 *
 * install once at startup with CRYPTO_ACCEL_Install(); it probes the
 * CPU at runtime (cpuid on x86, getauxval/AT_HWCAP on Linux aarch64,
 * compile-time trust on Apple Silicon / Windows-on-ARM) and refuses
 * to install (-1) when the instructions are missing, so the built-in
 * software implementation stays active.
 *
 * compile (no special flags needed, the ISA is enabled per-branch):
 *   gcc/clang x86 : works with or without -maes/-mpclmul (the hot
 *                   functions carry a target("sse2,aes,pclmul") attr)
 *   gcc aarch64   : #pragma GCC target("+crypto") for the whole file,
 *                   no -march needed
 *   clang aarch64 : #pragma clang attribute target("crypto")
 *   MSVC          : SHA intrinsics require compiling as C++ (/TP);
 *                   the C front end rejects by-value __m128i
 *                   intrinsic parameters (C2718). x86 AVX/AES paths
 *                   are fine as C
 * the file may be compiled by a C++ driver as well (g++ treats .c as
 * C++); the entry points keep C linkage via plugin_secure.h.
 *
 * GHASH strategy (identical math on both platforms): the 16 input
 * bytes are bit-reversed per byte (VRBIT on arm, masked shifts on x86)
 * so the vector little-endian bit numbering matches the GCM
 * polynomial convention (bit j = coefficient of t^j, GCM bit 0 is the
 * MSB of byte 0), then a schoolbook 4-way carry-less multiplication
 * produces the 256-bit product, reduced by t^128 = t^7+t^2+t+1 in two
 * folding steps. H is pre-reversed once by the hashkey callback.
 *
 * the ARM AESE instruction applies the round key BEFORE SubBytes and
 * ShiftRows (SR(SB(state ^ key)), no MixColumns), so the round
 * sequence differs from x86: no initial whitening eor, aese/aesmc
 * pairs consume round keys 0..n-2, the last aese consumes key n-1 and
 * the final round key n is applied with a plain eor. this sequence is
 * algebraically equivalent to FIPS-197 (proof by induction: after
 * round i the ARM state equals the standard state XOR k_i, and the
 * final aese+eor cancels exactly).
 *
 * correctness: see accel_test.c (A/B differential against the
 * software implementation + NIST anchors); the x86 branch is also
 * validated by 23625 NIST CAVP vectors via AEAD_KAT_AESNI=1
 * (aead_kat.c) and the gtest suite via ISECURE_AESNI=1.
 *
 * integration: compile together with isecure.c; isecure itself never
 * includes or links this file - it only fills the callback slots
 * declared in isecure.h. see AGENTS.md for the plugin_ conventions.
 */

#include "../../system/isecure.h"
#include "plugin_secure.h"

static const char *g_accel_backend = NULL;

//=====================================================================
// x86 / x64 backend: AES-NI + PCLMULQDQ
//=====================================================================
#if defined(__i386__) || defined(__x86_64__) || \
	defined(_M_IX86) || defined(_M_X64)

#if defined(_MSC_VER) && _MSC_VER < 1600
	#define ACCEL_NO_BACKEND	/* VC2008-: no wmmintrin.h */
#endif

#ifndef ACCEL_NO_BACKEND

#include <immintrin.h>   /* SSE2/AES/CLMUL/AVX2 intrinsics */
#if defined(_MSC_VER)
	#include <intrin.h>
	#define ACCEL_TARGET
#else
	#include <cpuid.h>
	#define ACCEL_TARGET __attribute__((target("sse2,aes,pclmul")))
#endif

/* reverse the bits inside each of the 16 bytes (byte order kept):
 * after this the SSE2 little-endian bit numbering matches the GCM
 * polynomial convention. the 16-bit lane shifts cannot cross byte
 * boundaries because the masks keep the relevant lane bits zero. */
ACCEL_TARGET static inline __m128i accel_x86_bitrev8(__m128i v)
{
	const __m128i m1 = _mm_set1_epi8(0x55);
	const __m128i m2 = _mm_set1_epi8(0x33);
	const __m128i m4 = _mm_set1_epi8(0x0f);
	__m128i t;
	t = _mm_or_si128(
		_mm_slli_epi16(_mm_and_si128(v, m1), 1),
		_mm_and_si128(_mm_srli_epi16(v, 1), m1));
	t = _mm_or_si128(
		_mm_slli_epi16(_mm_and_si128(t, m2), 2),
		_mm_and_si128(_mm_srli_epi16(t, 2), m2));
	t = _mm_or_si128(
		_mm_slli_epi16(_mm_and_si128(t, m4), 4),
		_mm_and_si128(_mm_srli_epi16(t, 4), m4));
	return t;
}

/* 128-bit polynomial left shift by k (0 < k < 64), overflow dropped.
 * k must be a literal: the shift intrinsics take an immediate. */
#define ACCEL_X86_SLLI128(v, k) \
	_mm_or_si128(_mm_slli_epi64((v), (k)), \
		_mm_slli_si128(_mm_srli_epi64((v), 64 - (k)), 8))

/* 128-bit right shift by k (64 < k < 128): top (128-k) bits of v
 * moved into the low bits. k must be a literal. */
#define ACCEL_X86_SRLI128(v, k) \
	_mm_srli_si128(_mm_srli_epi64((v), (k) - 64), 8)

/* on little-endian machines the in-memory byte order of key_enc[]
 * (little-endian u32 groups written by CRYPTO_AES_Init) equals the
 * FIPS-197 round key, so AESENC can consume it directly. use
 * explicit unaligned loads: a plain __m128i dereference lets the
 * compiler emit aligned movdqa, which crashes on i386 where the ctx
 * is only 4-byte aligned. */
ACCEL_TARGET static void accel_x86_encrypt(CRYPTO_AES_CTX *ctx,
		IUINT8 *out, const IUINT8 *in)
{
	const __m128i *rk = (const __m128i *)ctx->key_enc;
	int rounds = 6 + (int)(ctx->key_length / 4);
	int i;
	__m128i m = _mm_xor_si128(
		_mm_loadu_si128((const __m128i *)in),
		_mm_loadu_si128(rk));
	for (i = 1; i < rounds; i++) {
		m = _mm_aesenc_si128(m, _mm_loadu_si128(rk + i));
	}
	m = _mm_aesenclast_si128(m, _mm_loadu_si128(rk + rounds));
	_mm_storeu_si128((__m128i *)out, m);
}

/* pre-transform H in place: bit-reverse each byte (see above) */
ACCEL_TARGET static void accel_x86_hashkey(IUINT8 H[16])
{
	__m128i h = _mm_loadu_si128((const __m128i *)H);
	_mm_storeu_si128((__m128i *)H, accel_x86_bitrev8(h));
}

/* X = X * H in GF(2^128), GCM convention. H must be pre-transformed
 * by accel_x86_hashkey. 4x pclmulqdq schoolbook product, then reduce
 * mod p(t) = t^128+t^7+t^2+t+1: hi*r (r = t^7+t^2+t+1) has degree up
 * to 134, its 7 overflow bits are collected and folded once more. */
ACCEL_TARGET static void accel_x86_mul(IUINT8 X[16], const IUINT8 H[16])
{
	__m128i x, h, lo, hi, mid, e_lo, e_hi, c;

	x = accel_x86_bitrev8(_mm_loadu_si128((const __m128i *)X));
	h = _mm_loadu_si128((const __m128i *)H);

	lo = _mm_clmulepi64_si128(x, h, 0x00);
	hi = _mm_clmulepi64_si128(x, h, 0x11);
	mid = _mm_xor_si128(
		_mm_clmulepi64_si128(x, h, 0x01),
		_mm_clmulepi64_si128(x, h, 0x10));
	lo = _mm_xor_si128(lo, _mm_slli_si128(mid, 8));
	hi = _mm_xor_si128(hi, _mm_srli_si128(mid, 8));

	e_lo = _mm_xor_si128(hi, ACCEL_X86_SLLI128(hi, 1));
	e_lo = _mm_xor_si128(e_lo, ACCEL_X86_SLLI128(hi, 2));
	e_lo = _mm_xor_si128(e_lo, ACCEL_X86_SLLI128(hi, 7));
	e_hi = _mm_xor_si128(ACCEL_X86_SRLI128(hi, 121),
		ACCEL_X86_SRLI128(hi, 126));
	e_hi = _mm_xor_si128(e_hi, ACCEL_X86_SRLI128(hi, 127));

	c = _mm_xor_si128(lo, e_lo);
	c = _mm_xor_si128(c, e_hi);
	c = _mm_xor_si128(c, ACCEL_X86_SLLI128(e_hi, 1));
	c = _mm_xor_si128(c, ACCEL_X86_SLLI128(e_hi, 2));
	c = _mm_xor_si128(c, ACCEL_X86_SLLI128(e_hi, 7));

	_mm_storeu_si128((__m128i *)X, accel_x86_bitrev8(c));
}

/* returns 1 when the CPU has both AES-NI and PCLMULQDQ. only XMM
 * state is touched (no YMM), which plain FXSAVE covers, so no
 * XCR0/xgetbv probing is needed. */
static int accel_x86_available(void)
{
#if defined(_MSC_VER)
	int info[4];
	__cpuid(info, 1);
	return (info[2] & (1u << 25)) != 0 &&   /* AESNI */
	       (info[2] & (1u << 1)) != 0;      /* PCLMULQDQ */
#else
	unsigned int a, b, c, d;
	if (!__get_cpuid(1, &a, &b, &c, &d)) {
		return 0;
	}
	(void)a; (void)b; (void)d;
	return (c & (1u << 25)) != 0 &&   /* AESNI */
	       (c & (1u << 1)) != 0;      /* PCLMULQDQ */
#endif
}

//---------------------------------------------------------------------
// x86 AVX2 backend: chacha20 bulk keystream + poly1305 blocks
//---------------------------------------------------------------------
#if !defined(_MSC_VER)
	#define ACCEL_VEC_TARGET __attribute__((target("avx2")))
	/* read XCR0: reports which register states the OS saves. unlike
	 * the AES-NI path, AVX2 touches YMM, so the OS must have XSAVE
	 * enabled (any Win7 SP1+ / modern Linux does) */
	static inline unsigned long long accel_xgetbv(void)
	{
		unsigned int lo, hi;
		__asm__ volatile ("xgetbv" : "=a" (lo), "=d" (hi) : "c" (0));
		return ((unsigned long long)hi << 32) | lo;
	}
#else
	#define ACCEL_VEC_TARGET
	#include <immintrin.h>
	/* the intrinsics below emit AVX2 code without /arch:AVX, which
	 * is fine: these functions are only called after the runtime
	 * cpuid/xgetbv check in accel_x86_have_avx2 passed */
	#pragma warning(push)
	#pragma warning(disable: 4752)
	#define accel_xgetbv() _xgetbv(0)
#endif

/* returns 1 when AVX2 is usable: leaf1 AVX + OSXSAVE + XCR0 XMM/YMM
 * enabled + leaf7 EBX bit5 (AVX2) */
static int accel_x86_have_avx2(void)
{
#if defined(_MSC_VER)
	int info[4];
	__cpuid(info, 1);
	if ((info[2] & (1u << 28)) == 0) return 0;   /* AVX */
	if ((info[2] & (1u << 27)) == 0) return 0;   /* OSXSAVE */
	if ((accel_xgetbv() & 0x6) != 0x6) return 0; /* XMM+YMM enabled */
	__cpuidex(info, 7, 0);
	return (info[1] & (1 << 5)) != 0;            /* AVX2 */
#else
	unsigned int a, b, c, d;
	if (!__get_cpuid(1, &a, &b, &c, &d)) {
		return 0;
	}
	(void)a; (void)b; (void)d;
	if ((c & (1u << 28)) == 0) return 0;   /* AVX */
	if ((c & (1u << 27)) == 0) return 0;   /* OSXSAVE */
	if ((accel_xgetbv() & 0x6) != 0x6) return 0;  /* XMM+YMM enabled */
	{
		unsigned int a7, b7, c7, d7;
		if (!__get_cpuid_count(7, 0, &a7, &b7, &c7, &d7)) {
			return 0;
		}
		(void)a7; (void)c7; (void)d7;
		return (b7 & (1u << 5)) != 0;      /* AVX2 */
	}
#endif
}

/* 32-bit rotate-left of a 256-bit vector of dwords (AVX2 has no
 * vprord; the AVX-512 one only exists on newer CPUs) */
#define ACCEL_ROTL32(v, n) \
	_mm256_or_si256(_mm256_slli_epi32((v), (n)), \
		_mm256_srli_epi32((v), 32 - (n)))

/* one chacha quarter round over four ymm registers, each holding
 * the same state word of 8 independent blocks */
#define ACCEL_CHACHA_QROUND(a, b, c, d) \
	do { \
		a = _mm256_add_epi32(a, b); \
		d = _mm256_xor_si256(d, a); \
		d = ACCEL_ROTL32(d, 16); \
		c = _mm256_add_epi32(c, d); \
		b = _mm256_xor_si256(b, c); \
		b = ACCEL_ROTL32(b, 12); \
		a = _mm256_add_epi32(a, b); \
		d = _mm256_xor_si256(d, a); \
		d = ACCEL_ROTL32(d, 8); \
		c = _mm256_add_epi32(c, d); \
		b = _mm256_xor_si256(b, c); \
		b = ACCEL_ROTL32(b, 7); \
	} while (0)

/* CRYPTO_CHACHA20_BLOCKS_CB: 8 blocks per pass. the 16 ymm hold one
 * state word of 8 blocks each (state[0..11,13..15] broadcast,
 * state[12] = counter + 0..7), 10 double rounds run in parallel,
 * then the words are added back to the original state and the
 * word-major matrix is transposed into the output. blocks < 8 waste
 * lanes but stay correct (only the first 'blocks' columns are
 * stored), which keeps small updates and the Update tail simple. */
ACCEL_VEC_TARGET static void accel_x86_chacha_blocks(
		CRYPTO_CHACHA20_CTX *ctx, IUINT8 *out, size_t blocks)
{
	__m256i x[16];
	IUINT32 base[16];
	size_t done = 0;
	int i;

	for (i = 0; i < 16; i++) {
		base[i] = ctx->state[i];
	}
	while (blocks > 0) {
		size_t n = blocks > 8 ? 8 : blocks;
		IUINT32 w[16 * 8];   /* storeu targets: no alignment needed */
		__m256i ctr_vec;
		int r;
		for (i = 0; i < 16; i++) {
			x[i] = _mm256_set1_epi32((int)base[i]);
		}
		x[12] = _mm256_add_epi32(x[12],
			_mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
		/* the final word add-back needs each block's OWN initial
		 * counter (base + lane), like the scalar block generator
		 * re-adding state[12] before incrementing it */
		ctr_vec = x[12];

		for (r = 0; r < 10; r++) {
			ACCEL_CHACHA_QROUND(x[0], x[4], x[8], x[12]);
			ACCEL_CHACHA_QROUND(x[1], x[5], x[9], x[13]);
			ACCEL_CHACHA_QROUND(x[2], x[6], x[10], x[14]);
			ACCEL_CHACHA_QROUND(x[3], x[7], x[11], x[15]);
			ACCEL_CHACHA_QROUND(x[0], x[5], x[10], x[15]);
			ACCEL_CHACHA_QROUND(x[1], x[6], x[11], x[12]);
			ACCEL_CHACHA_QROUND(x[2], x[7], x[8], x[13]);
			ACCEL_CHACHA_QROUND(x[3], x[4], x[9], x[14]);
		}
		for (i = 0; i < 16; i++) {
			__m256i add = (i == 12) ? ctr_vec :
				_mm256_set1_epi32((int)base[i]);
			_mm256_storeu_si256((__m256i *)(w + i * 8),
				_mm256_add_epi32(x[i], add));
		}
		/* w[i*8+b] is word i of block b: transpose to out */
		{
			size_t b;
			for (b = 0; b < n; b++) {
				IUINT32 *o = (IUINT32 *)(out + done * 64 + b * 64);
				for (i = 0; i < 16; i++) {
					o[i] = w[i * 8 + b];
				}
			}
		}
		base[12] += (IUINT32)n;
		done += n;
		blocks -= n;
	}
	ctx->state[12] = base[12];
}

/* little-endian u32 load (isecure.c's is_pack4 is file-private) */
static inline IUINT32 accel_load_le32(const IUINT8 *p)
{
	return (IUINT32)p[0] | ((IUINT32)p[1] << 8) |
		((IUINT32)p[2] << 16) | ((IUINT32)p[3] << 24);
}

/* CRYPTO_POLY1305_BLOCK_CB, scalar fallback for single blocks: same
 * 26-bit limb math as the built-in crypto_poly1305_block (the
 * partial reduction form is part of the callback contract) */
static void accel_x86_poly_block_scalar(CRYPTO_POLY1305_CTX *ctx,
		const IUINT8 *m)
{
	IUINT32 r0 = ctx->poly_r[0], r1 = ctx->poly_r[1], r2 = ctx->poly_r[2];
	IUINT32 r3 = ctx->poly_r[3], r4 = ctx->poly_r[4];
	IUINT32 s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
	IUINT32 h0 = ctx->poly_h[0], h1 = ctx->poly_h[1], h2 = ctx->poly_h[2];
	IUINT32 h3 = ctx->poly_h[3], h4 = ctx->poly_h[4];
	IUINT32 c;
	IUINT64 d0, d1, d2, d3, d4;

	h0 += accel_load_le32(m + 0) & 0x3ffffff;
	h1 += (accel_load_le32(m + 3) >> 2) & 0x3ffffff;
	h2 += (accel_load_le32(m + 6) >> 4) & 0x3ffffff;
	h3 += (accel_load_le32(m + 9) >> 6) & 0x3ffffff;
	h4 += (accel_load_le32(m + 12) >> 8) | (1u << 24);

	d0 = ((IUINT64)h0 * r0) + ((IUINT64)h1 * s4) + ((IUINT64)h2 * s3)
		+ ((IUINT64)h3 * s2) + ((IUINT64)h4 * s1);
	d1 = ((IUINT64)h0 * r1) + ((IUINT64)h1 * r0) + ((IUINT64)h2 * s4)
		+ ((IUINT64)h3 * s3) + ((IUINT64)h4 * s2);
	d2 = ((IUINT64)h0 * r2) + ((IUINT64)h1 * r1) + ((IUINT64)h2 * r0)
		+ ((IUINT64)h3 * s4) + ((IUINT64)h4 * s3);
	d3 = ((IUINT64)h0 * r3) + ((IUINT64)h1 * r2) + ((IUINT64)h2 * r1)
		+ ((IUINT64)h3 * r0) + ((IUINT64)h4 * s4);
	d4 = ((IUINT64)h0 * r4) + ((IUINT64)h1 * r3) + ((IUINT64)h2 * r2)
		+ ((IUINT64)h3 * r1) + ((IUINT64)h4 * r0);

	d1 += c = (IUINT32)(d0 >> 26); h0 = (IUINT32)d0 & 0x3ffffff;
	d2 += c = (IUINT32)(d1 >> 26); h1 = (IUINT32)d1 & 0x3ffffff;
	d3 += c = (IUINT32)(d2 >> 26); h2 = (IUINT32)d2 & 0x3ffffff;
	d4 += c = (IUINT32)(d3 >> 26); h3 = (IUINT32)d3 & 0x3ffffff;
	c = (IUINT32)(d4 >> 26); h4 = (IUINT32)d4 & 0x3ffffff;
	h0 += c * 5; c = (h0 >> 26); h0 &= 0x3ffffff;
	h1 += c;

	ctx->poly_h[0] = h0;
	ctx->poly_h[1] = h1;
	ctx->poly_h[2] = h2;
	ctx->poly_h[3] = h3;
	ctx->poly_h[4] = h4;
}

/* CRYPTO_POLY1305_BLOCK_CB: per block h = (h + m + 2^128) * r.
 * vector layout: each vpmuludq multiplies broadcast h_i against the
 * rotation vector RV_i and lands partial sums of FOUR different d_j
 * (j=0..3) into the four 64-bit lanes of D, so no horizontal
 * reduction is needed; d4 is computed in scalars. the rotation
 * vectors are built once per call and amortized over the blocks. */
ACCEL_VEC_TARGET static void accel_x86_poly_blocks(
		CRYPTO_POLY1305_CTX *ctx, const IUINT8 *m, size_t nblocks)
{
	IUINT32 r0 = ctx->poly_r[0], r1 = ctx->poly_r[1], r2 = ctx->poly_r[2];
	IUINT32 r3 = ctx->poly_r[3], r4 = ctx->poly_r[4];
	IUINT32 s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
	/* RV_i lane j (64-bit slot j, low dword) = coefficient of h_i in
	 * d_j: rows are d0=[r0,s4,s3,s2,s1], d1=[r1,r0,s4,s3,s2],
	 * d2=[r2,r1,r0,s4,s3], d3=[r3,r2,r1,r0,s4] for i=0..4 */
	__m256i rv[5];
	size_t n;

	if (nblocks <= 1) {
		accel_x86_poly_block_scalar(ctx, m);
		return;
	}

	/* RV_i holds [rr_{0,i}, rr_{1,i}, rr_{2,i}, rr_{3,i}] in the low
	 * dword of each 64-bit slot (vpmuludq reads those) */
	rv[0] = _mm256_setr_epi32((int)r0, 0, (int)r1, 0, (int)r2, 0, (int)r3, 0);
	rv[1] = _mm256_setr_epi32((int)s4, 0, (int)r0, 0, (int)r1, 0, (int)r2, 0);
	rv[2] = _mm256_setr_epi32((int)s3, 0, (int)s4, 0, (int)r0, 0, (int)r1, 0);
	rv[3] = _mm256_setr_epi32((int)s2, 0, (int)s3, 0, (int)s4, 0, (int)r0, 0);
	rv[4] = _mm256_setr_epi32((int)s1, 0, (int)s2, 0, (int)s3, 0, (int)s4, 0);

	for (n = 0; n < nblocks; n++, m += 16) {
		IUINT32 h0 = ctx->poly_h[0], h1 = ctx->poly_h[1];
		IUINT32 h2 = ctx->poly_h[2], h3 = ctx->poly_h[3];
		IUINT32 h4 = ctx->poly_h[4];
		__m256i dvec;
		IUINT64 d[5];
		IUINT32 c;

		/* h += m (+2^128 in limb 4) */
		h0 += accel_load_le32(m + 0) & 0x3ffffff;
		h1 += (accel_load_le32(m + 3) >> 2) & 0x3ffffff;
		h2 += (accel_load_le32(m + 6) >> 4) & 0x3ffffff;
		h3 += (accel_load_le32(m + 9) >> 6) & 0x3ffffff;
		h4 += (accel_load_le32(m + 12) >> 8) | (1u << 24);

		/* D lane j = sum_i h_i * RV_i[j], i.e. partial sums of
		 * d0..d3 (each term is at most 2^52.3, five per lane) */
		dvec = _mm256_mul_epu32(_mm256_set1_epi32((int)h0), rv[0]);
		dvec = _mm256_add_epi64(dvec,
			_mm256_mul_epu32(_mm256_set1_epi32((int)h1), rv[1]));
		dvec = _mm256_add_epi64(dvec,
			_mm256_mul_epu32(_mm256_set1_epi32((int)h2), rv[2]));
		dvec = _mm256_add_epi64(dvec,
			_mm256_mul_epu32(_mm256_set1_epi32((int)h3), rv[3]));
		dvec = _mm256_add_epi64(dvec,
			_mm256_mul_epu32(_mm256_set1_epi32((int)h4), rv[4]));

		_mm256_storeu_si256((__m256i *)d, dvec);
		/* d4 = [r4, r3, r2, r1, r0] . [h0..h4] */
		d[4] = ((IUINT64)h0 * r4) + ((IUINT64)h1 * r3) +
			((IUINT64)h2 * r2) + ((IUINT64)h3 * r1) + ((IUINT64)h4 * r0);

		/* (partial) carry propagation, identical to the scalar path */
		d[1] += c = (IUINT32)(d[0] >> 26); h0 = (IUINT32)d[0] & 0x3ffffff;
		d[2] += c = (IUINT32)(d[1] >> 26); h1 = (IUINT32)d[1] & 0x3ffffff;
		d[3] += c = (IUINT32)(d[2] >> 26); h2 = (IUINT32)d[2] & 0x3ffffff;
		d[4] += c = (IUINT32)(d[3] >> 26); h3 = (IUINT32)d[3] & 0x3ffffff;
		c = (IUINT32)(d[4] >> 26); h4 = (IUINT32)d[4] & 0x3ffffff;
		h0 += c * 5; c = (h0 >> 26); h0 &= 0x3ffffff;
		h1 += c;

		ctx->poly_h[0] = h0;
		ctx->poly_h[1] = h1;
		ctx->poly_h[2] = h2;
		ctx->poly_h[3] = h3;
		ctx->poly_h[4] = h4;
	}
}

#if defined(_MSC_VER)
	#pragma warning(pop)   /* C4752 AVX hint */
#endif

//---------------------------------------------------------------------
// x86 SHA-NI backend: sha1/sha256 single-block compression
// (adapted from Jeffrey Walton's public-domain SHA-Intrinsics,
// itself based on Intel and miTLS sample code - the round structure
// is the canonical SHA256RNDS2/SHA256MSG/SHA1RNDS4 sequence)
//---------------------------------------------------------------------
#if !defined(_MSC_VER)
	#define ACCEL_SHANI_TARGET __attribute__((target("sha,sse4.1")))
#else
	#define ACCEL_SHANI_TARGET
#endif

/* returns 1 when the CPU supports Intel SHA extensions
 * (leaf 7 EBX bit 25) - independent of the AES and AVX2 groups */
static int accel_x86_have_shani(void)
{
#if defined(_MSC_VER)
	int info[4];
	__cpuid(info, 0);
	if (info[0] < 7) return 0;
	__cpuidex(info, 7, 0);
	return (info[1] & (1u << 25)) != 0;
#else
	unsigned int a, b, c, d;
	if (__get_cpuid_max(0, NULL) < 7) return 0;
	if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
	(void)a; (void)c; (void)d;
	return (b & (1u << 25)) != 0;
#endif
}

/* HASH_SHA256_COMPRESS_CB: one 64-byte block, standard state layout.
 * state load/store is done per call (single-block granularity),
 * costing a handful of shuffles per block. */
ACCEL_SHANI_TARGET static void accel_x86_sha256_compress(
		IUINT32 state[8], const IUINT8 data[64])
{
	__m128i STATE0, STATE1, MSG, TMP;
	__m128i MSG0, MSG1, MSG2, MSG3;
	__m128i ABEF_SAVE, CDGH_SAVE;
	const __m128i MASK = _mm_set_epi64x(0x0c0d0e0f08090a0bULL,
		0x0405060700010203ULL);

	/* load state: STATE0 = ABEF, STATE1 = CDGH */
	TMP = _mm_loadu_si128((const __m128i *)&state[0]);
	STATE1 = _mm_loadu_si128((const __m128i *)&state[4]);
	TMP = _mm_shuffle_epi32(TMP, 0xB1);          /* CDAB */
	STATE1 = _mm_shuffle_epi32(STATE1, 0x1B);    /* EFGH */
	STATE0 = _mm_alignr_epi8(TMP, STATE1, 8);    /* ABEF */
	STATE1 = _mm_blend_epi16(STATE1, TMP, 0xF0); /* CDGH */

	ABEF_SAVE = STATE0;
	CDGH_SAVE = STATE1;

	/* rounds 0-3 */
	MSG0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 0)), MASK);
	MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(0xE9B5DBA5B5C0FBCFULL,
		0x71374491428A2F98ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

	/* rounds 4-7 */
	MSG1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 16)), MASK);
	MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(0xAB1C5ED5923F82A4ULL,
		0x59F111F13956C25BULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

	/* rounds 8-11 */
	MSG2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 32)), MASK);
	MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(0x550C7DC3243185BEULL,
		0x12835B01D807AA98ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

	/* rounds 12-15 */
	MSG3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 48)), MASK);
	MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(0xC19BF1749BDC06A7ULL,
		0x80DEB1FE72BE5D74ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
	MSG0 = _mm_add_epi32(MSG0, TMP);
	MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

	/* rounds 16-19 */
	MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(0x240CA1CC0FC19DC6ULL,
		0xEFBE4786E49B69C1ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
	MSG1 = _mm_add_epi32(MSG1, TMP);
	MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

	/* rounds 20-23 */
	MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(0x76F988DA5CB0A9DCULL,
		0x4A7484AA2DE92C6FULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
	MSG2 = _mm_add_epi32(MSG2, TMP);
	MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

	/* rounds 24-27 */
	MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(0xBF597FC7B00327C8ULL,
		0xA831C66D983E5152ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
	MSG3 = _mm_add_epi32(MSG3, TMP);
	MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

	/* rounds 28-31 */
	MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(0x1429296706CA6351ULL,
		0xD5A79147C6E00BF3ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
	MSG0 = _mm_add_epi32(MSG0, TMP);
	MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

	/* rounds 32-35 */
	MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(0x53380D134D2C6DFCULL,
		0x2E1B213827B70A85ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
	MSG1 = _mm_add_epi32(MSG1, TMP);
	MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

	/* rounds 36-39 */
	MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(0x92722C8581C2C92EULL,
		0x766A0ABB650A7354ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
	MSG2 = _mm_add_epi32(MSG2, TMP);
	MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

	/* rounds 40-43 */
	MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(0xC76C51A3C24B8B70ULL,
		0xA81A664BA2BFE8A1ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
	MSG3 = _mm_add_epi32(MSG3, TMP);
	MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

	/* rounds 44-47 */
	MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(0x106AA070F40E3585ULL,
		0xD6990624D192E819ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
	MSG0 = _mm_add_epi32(MSG0, TMP);
	MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

	/* rounds 48-51 */
	MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(0x34B0BCB52748774CULL,
		0x1E376C0819A4C116ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
	MSG1 = _mm_add_epi32(MSG1, TMP);
	MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
	MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

	/* rounds 52-55 */
	MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(0x682E6FF35B9CCA4FULL,
		0x4ED8AA4A391C0CB3ULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
	MSG2 = _mm_add_epi32(MSG2, TMP);
	MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

	/* rounds 56-59 */
	MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(0x8CC7020884C87814ULL,
		0x78A5636F748F82EEULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
	MSG3 = _mm_add_epi32(MSG3, TMP);
	MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

	/* rounds 60-63 */
	MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(0xC67178F2BEF9A3F7ULL,
		0xA4506CEB90BEFFFAULL));
	STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
	MSG = _mm_shuffle_epi32(MSG, 0x0E);
	STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

	/* combine state */
	STATE0 = _mm_add_epi32(STATE0, ABEF_SAVE);
	STATE1 = _mm_add_epi32(STATE1, CDGH_SAVE);

	/* store state (inverse of the load shuffles) */
	TMP = _mm_shuffle_epi32(STATE0, 0x1B);       /* FEBA */
	STATE1 = _mm_shuffle_epi32(STATE1, 0xB1);    /* DCHG */
	STATE0 = _mm_blend_epi16(TMP, STATE1, 0xF0); /* DCBA */
	STATE1 = _mm_alignr_epi8(STATE1, TMP, 8);    /* HGFE */
	_mm_storeu_si128((__m128i *)&state[0], STATE0);
	_mm_storeu_si128((__m128i *)&state[4], STATE1);
}

/* HASH_SHA1_COMPRESS_CB: one 64-byte block, standard state[5] */
ACCEL_SHANI_TARGET static void accel_x86_sha1_compress(
		IUINT32 state[5], const IUINT8 data[64])
{
	__m128i ABCD, ABCD_SAVE, E0, E0_SAVE, E1;
	__m128i MSG0, MSG1, MSG2, MSG3;
	const __m128i MASK = _mm_set_epi64x(0x0001020304050607ULL,
		0x08090a0b0c0d0e0fULL);

	ABCD = _mm_loadu_si128((const __m128i *)state);
	E0 = _mm_set_epi32((int)state[4], 0, 0, 0);
	ABCD = _mm_shuffle_epi32(ABCD, 0x1B);

	ABCD_SAVE = ABCD;
	E0_SAVE = E0;

	/* rounds 0-3 */
	MSG0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 0)), MASK);
	E0 = _mm_add_epi32(E0, MSG0);
	E1 = ABCD;
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 0);

	/* rounds 4-7 */
	MSG1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 16)), MASK);
	E1 = _mm_sha1nexte_epu32(E1, MSG1);
	E0 = ABCD;
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 0);
	MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);

	/* rounds 8-11 */
	MSG2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 32)), MASK);
	E0 = _mm_sha1nexte_epu32(E0, MSG2);
	E1 = ABCD;
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 0);
	MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
	MSG0 = _mm_xor_si128(MSG0, MSG2);

	/* rounds 12-15 */
	MSG3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 48)), MASK);
	E1 = _mm_sha1nexte_epu32(E1, MSG3);
	E0 = ABCD;
	MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 0);
	MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
	MSG1 = _mm_xor_si128(MSG1, MSG3);

	/* rounds 16-19 */
	E0 = _mm_sha1nexte_epu32(E0, MSG0);
	E1 = ABCD;
	MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 0);
	MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
	MSG2 = _mm_xor_si128(MSG2, MSG0);

	/* rounds 20-23 */
	E1 = _mm_sha1nexte_epu32(E1, MSG1);
	E0 = ABCD;
	MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 1);
	MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);
	MSG3 = _mm_xor_si128(MSG3, MSG1);

	/* rounds 24-27 */
	E0 = _mm_sha1nexte_epu32(E0, MSG2);
	E1 = ABCD;
	MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 1);
	MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
	MSG0 = _mm_xor_si128(MSG0, MSG2);

	/* rounds 28-31 */
	E1 = _mm_sha1nexte_epu32(E1, MSG3);
	E0 = ABCD;
	MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 1);
	MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
	MSG1 = _mm_xor_si128(MSG1, MSG3);

	/* rounds 32-35 */
	E0 = _mm_sha1nexte_epu32(E0, MSG0);
	E1 = ABCD;
	MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 1);
	MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
	MSG2 = _mm_xor_si128(MSG2, MSG0);

	/* rounds 36-39 */
	E1 = _mm_sha1nexte_epu32(E1, MSG1);
	E0 = ABCD;
	MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 1);
	MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);
	MSG3 = _mm_xor_si128(MSG3, MSG1);

	/* rounds 40-43 */
	E0 = _mm_sha1nexte_epu32(E0, MSG2);
	E1 = ABCD;
	MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 2);
	MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
	MSG0 = _mm_xor_si128(MSG0, MSG2);

	/* rounds 44-47 */
	E1 = _mm_sha1nexte_epu32(E1, MSG3);
	E0 = ABCD;
	MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 2);
	MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
	MSG1 = _mm_xor_si128(MSG1, MSG3);

	/* rounds 48-51 */
	E0 = _mm_sha1nexte_epu32(E0, MSG0);
	E1 = ABCD;
	MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 2);
	MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
	MSG2 = _mm_xor_si128(MSG2, MSG0);

	/* rounds 52-55 */
	E1 = _mm_sha1nexte_epu32(E1, MSG1);
	E0 = ABCD;
	MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 2);
	MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);
	MSG3 = _mm_xor_si128(MSG3, MSG1);

	/* rounds 56-59 */
	E0 = _mm_sha1nexte_epu32(E0, MSG2);
	E1 = ABCD;
	MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 2);
	MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
	MSG0 = _mm_xor_si128(MSG0, MSG2);

	/* rounds 60-63 */
	E1 = _mm_sha1nexte_epu32(E1, MSG3);
	E0 = ABCD;
	MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 3);
	MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
	MSG1 = _mm_xor_si128(MSG1, MSG3);

	/* rounds 64-67 */
	E0 = _mm_sha1nexte_epu32(E0, MSG0);
	E1 = ABCD;
	MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 3);
	MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
	MSG2 = _mm_xor_si128(MSG2, MSG0);

	/* rounds 68-71 */
	E1 = _mm_sha1nexte_epu32(E1, MSG1);
	E0 = ABCD;
	MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 3);
	MSG3 = _mm_xor_si128(MSG3, MSG1);

	/* rounds 72-75 */
	E0 = _mm_sha1nexte_epu32(E0, MSG2);
	E1 = ABCD;
	MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
	ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 3);

	/* rounds 76-79 */
	E1 = _mm_sha1nexte_epu32(E1, MSG3);
	E0 = ABCD;
	ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 3);

	/* combine state */
	E0 = _mm_sha1nexte_epu32(E0, E0_SAVE);
	ABCD = _mm_add_epi32(ABCD, ABCD_SAVE);

	/* store state */
	ABCD = _mm_shuffle_epi32(ABCD, 0x1B);
	_mm_storeu_si128((__m128i *)state, ABCD);
	state[4] = (IUINT32)_mm_extract_epi32(E0, 3);
}

#define ACCEL_HAVE_X86

#endif /* !ACCEL_NO_BACKEND */

//=====================================================================
// aarch64 backend: ARMv8 Crypto Extensions
//=====================================================================
#elif defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>

#if defined(_MSC_VER)
	/* MSVC ARM64: all intrinsics available unconditionally */
#elif defined(__clang__)
	#pragma clang attribute push \
		(__attribute__((target("crypto"))), apply_to = function)
	#define ACCEL_ARM_CLANG_POP
#elif defined(__GNUC__)
	#pragma GCC push_options
	#pragma GCC target("+crypto")
	#define ACCEL_ARM_GCC_POP
#endif

/* VRBIT (ARMv8.0 A64): reverse the bits inside each of the 16 bytes,
 * exactly the GCM convention transform needed (one instruction,
 * replacing the x86 3-step masked-shift version). requires
 * gcc 9+ / clang 4+ / MSVC 2019+ for the intrinsic. */
/* 128-bit polynomial left shift by k (0 < k < 64), overflow dropped.
 * k must be a literal: NEON shift intrinsics take an immediate. */
#define ACCEL_ARM_SLLI128(v, k) \
	vorrq_u64(vshlq_n_u64((v), (k)), \
		vextq_u64(vdupq_n_u64(0), \
			vshrq_n_u64((v), 64 - (k)), 1))

/* 128-bit right shift by k (64 < k < 128) */
#define ACCEL_ARM_SRLI128(v, k) \
	vextq_u64(vshrq_n_u64((v), (k) - 64), vdupq_n_u64(0), 1)

/* vmull_p64's poly types differ per compiler: gcc uses distinct
 * poly64_t/poly128_t vector types, clang declares poly128_t as
 * unsigned __int128, MSVC uses plain uint64_t/uint64x2_t. normalize
 * the result to uint64x2_t via a cast through uint8x16_t (bitwise
 * identical in all cases). */
#if defined(__clang__)
	#define ACCEL_VMULL(a, b) \
		vreinterpretq_u64_u8((uint8x16_t)vmull_p64((a), (b)))
#elif defined(__GNUC__)
	#define ACCEL_VMULL(a, b) \
		vreinterpretq_u64_u8((uint8x16_t)vmull_p64( \
			(poly64_t)(uint64_t)(a), (poly64_t)(uint64_t)(b)))
#else
	#define ACCEL_VMULL(a, b) vmull_p64((a), (b))
#endif

/* ARM AESE applies the round key BEFORE SubBytes/ShiftRows, so the
 * equivalent-to-FIPS sequence is: no whitening eor, n-1 aese/aesmc
 * pairs with round keys 0..n-2, one final aese with key n-1 and a
 * plain eor with the last round key n. round keys come straight from
 * key_enc[] (little-endian u32 groups; vld1q_u8 is unaligned-safe,
 * matching the layout AESE expects). */
static void accel_arm_encrypt(CRYPTO_AES_CTX *ctx,
		IUINT8 *out, const IUINT8 *in)
{
	const IUINT8 *rk = (const IUINT8 *)ctx->key_enc;
	int rounds = 6 + (int)(ctx->key_length / 4);
	int i;
	uint8x16_t m = vld1q_u8(in);
	for (i = 0; i < rounds - 1; i++) {
		m = vaesmcq_u8(vaeseq_u8(m, vld1q_u8(rk + i * 16)));
	}
	m = vaeseq_u8(m, vld1q_u8(rk + (rounds - 1) * 16));
	m = veorq_u8(m, vld1q_u8(rk + rounds * 16));
	vst1q_u8(out, m);
}

/* pre-transform H in place: vrbit each byte */
static void accel_arm_hashkey(IUINT8 H[16])
{
	vst1q_u8(H, vrbitq_u8(vld1q_u8(H)));
}

/* X = X * H in GF(2^128), GCM convention, same math as the x86
 * branch: vrbit into the polynomial convention, 4x PMULL schoolbook
 * product (vmull_p64 takes the 64-bit halves as scalars), two-step
 * reduction mod t^128+t^7+t^2+t+1, vrbit the result back. */
static void accel_arm_mul(IUINT8 X[16], const IUINT8 H[16])
{
	uint64x2_t x, h, lo, hi, mid, e_lo, e_hi, c;

	x = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8(X)));
	h = vreinterpretq_u64_u8(vld1q_u8(H));

	lo = ACCEL_VMULL(vgetq_lane_u64(x, 0), vgetq_lane_u64(h, 0));
	hi = ACCEL_VMULL(vgetq_lane_u64(x, 1), vgetq_lane_u64(h, 1));
	mid = veorq_u64(
		ACCEL_VMULL(vgetq_lane_u64(x, 1), vgetq_lane_u64(h, 0)),
		ACCEL_VMULL(vgetq_lane_u64(x, 0), vgetq_lane_u64(h, 1)));

	/* lo ^= mid << 64, hi ^= mid >> 64 */
	lo = veorq_u64(lo, vcombine_u64(vdup_n_u64(0),
		vcreate_u64(vgetq_lane_u64(mid, 0))));
	hi = veorq_u64(hi, vcombine_u64(
		vcreate_u64(vgetq_lane_u64(mid, 1)), vdup_n_u64(0)));

	e_lo = veorq_u64(hi, ACCEL_ARM_SLLI128(hi, 1));
	e_lo = veorq_u64(e_lo, ACCEL_ARM_SLLI128(hi, 2));
	e_lo = veorq_u64(e_lo, ACCEL_ARM_SLLI128(hi, 7));
	e_hi = veorq_u64(ACCEL_ARM_SRLI128(hi, 121),
		ACCEL_ARM_SRLI128(hi, 126));
	e_hi = veorq_u64(e_hi, ACCEL_ARM_SRLI128(hi, 127));

	c = veorq_u64(lo, e_lo);
	c = veorq_u64(c, e_hi);
	c = veorq_u64(c, ACCEL_ARM_SLLI128(e_hi, 1));
	c = veorq_u64(c, ACCEL_ARM_SLLI128(e_hi, 2));
	c = veorq_u64(c, ACCEL_ARM_SLLI128(e_hi, 7));

	vst1q_u8(X, vrbitq_u8(vreinterpretq_u8_u64(c)));
}

/* runtime detection: Linux/Android expose AT_HWCAP (the HWCAP_* bit
 * values below are the stable aarch64 ABI). Apple Silicon and
 * Windows-on-ARM CPUs always implement the Crypto Extensions, so the
 * compiler-level choice is trusted there. */
#if defined(__linux__) && !defined(_MSC_VER)
	#include <sys/auxv.h>
#endif

#ifndef ACCEL_HWCAP_ASIMD
	#define ACCEL_HWCAP_ASIMD (1UL << 1)
	#define ACCEL_HWCAP_AES   (1UL << 3)
	#define ACCEL_HWCAP_PMULL (1UL << 4)
#endif

static int accel_arm_available(void)
{
#if defined(__linux__) && !defined(_MSC_VER)
	unsigned long caps = getauxval(AT_HWCAP);
	return (caps & (ACCEL_HWCAP_ASIMD | ACCEL_HWCAP_AES |
		ACCEL_HWCAP_PMULL)) ==
		(ACCEL_HWCAP_ASIMD | ACCEL_HWCAP_AES | ACCEL_HWCAP_PMULL);
#else
	return 1;
#endif
}

#if defined(ACCEL_ARM_GCC_POP)
	#pragma GCC pop_options
	#undef ACCEL_ARM_GCC_POP
#endif
#if defined(ACCEL_ARM_CLANG_POP)
	#pragma clang attribute pop
	#undef ACCEL_ARM_CLANG_POP
#endif

#define ACCEL_HAVE_ARM

#endif /* aarch64 */

//=====================================================================
// install
//=====================================================================
int CRYPTO_ACCEL_Install(void)
{
	g_accel_backend = NULL;

#if defined(ACCEL_HAVE_X86)
	{
		int have_aes = accel_x86_available();
		int have_vec = accel_x86_have_avx2();
		int have_sha = accel_x86_have_shani();
		if (!have_aes && !have_vec && !have_sha) {
			return -1;
		}
		if (have_aes) {
			CRYPTO_AES_ENCRYPT_CB = accel_x86_encrypt;
			CRYPTO_GCM_HASHKEY_CB = accel_x86_hashkey;
			CRYPTO_GCM_MUL_CB = accel_x86_mul;
		}
		if (have_vec) {
			CRYPTO_CHACHA20_BLOCKS_CB = accel_x86_chacha_blocks;
			CRYPTO_POLY1305_BLOCK_CB = accel_x86_poly_blocks;
		}
		if (have_sha) {
			HASH_SHA1_COMPRESS_CB = accel_x86_sha1_compress;
			HASH_SHA256_COMPRESS_CB = accel_x86_sha256_compress;
		}
		{
			static const char *const names[8] = {
				"none", "x86 AES-NI + CLMUL", "x86 AVX2",
				"x86 AES-NI + CLMUL + AVX2", "x86 SHA-NI",
				"x86 AES-NI + CLMUL + SHA-NI", "x86 AVX2 + SHA-NI",
				"x86 AES-NI + CLMUL + AVX2 + SHA-NI"
			};
			int idx = (have_aes ? 1 : 0) | (have_vec ? 2 : 0) |
				(have_sha ? 4 : 0);
			g_accel_backend = names[idx];
		}
		return 0;
	}
#elif defined(ACCEL_HAVE_ARM)
	if (!accel_arm_available()) {
		return -1;
	}
	CRYPTO_AES_ENCRYPT_CB = accel_arm_encrypt;
	CRYPTO_GCM_HASHKEY_CB = accel_arm_hashkey;
	CRYPTO_GCM_MUL_CB = accel_arm_mul;
	g_accel_backend = "aarch64 Crypto Extensions";
	return 0;
#else
	return -2;
#endif
}

const char *CRYPTO_ACCEL_BackendName(void)
{
	return g_accel_backend;
}
