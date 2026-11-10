/*
 * plugin_secure.h - hardware acceleration plugin for isecure.
 *
 * one call at startup and every crypto primitive in isecure runs
 * 2.4x to 28x faster, if the CPU has the instructions:
 *
 *     CRYPTO_ACCEL_Install();   // once, before any crypto
 *
 * compile system/plugin_secure.c with your program. the plugin
 * detects the platform at build time (x86/x64 or aarch64) and
 * the CPU at run time (cpuid / getauxval), then fills the
 * CRYPTO_xxx / HASH_xxx callback slots declared in isecure.h:
 * AES-GCM, AES, ChaCha20-Poly1305, ChaCha20, SHA-1, SHA-256
 * (and HMAC/HKDF for free, they sit on top of these). when the
 * hardware does not support a group, isecure silently keeps its
 * built-in portable C implementation and Install reports it.
 *
 * measured throughput, MB/s (gcc -O2, 16MB buffer, win10 x64,
 * tool: scratch/crypto/accel_bench.c, checksums cross-checked):
 *
 *                 mingw64 x64        mingw32 x86
 *  algorithm      soft    hw   gain   soft    hw   gain
 *  -----------  ------ ------ -----  ------ ------ -----
 *  AES-ECB        205    977   4.8x    189    674   3.6x
 *  AES-GCM         12    329    28x     11    290    26x
 *  CHACHA20       240    717   3.0x    209    548   2.6x
 *  CHACHA-POLY    207    528   2.6x    169    409   2.4x
 *  SHA-1         1039   2375   2.3x    862   2446   2.8x
 *  SHA-256        363   2158   5.9x    309   2100   6.8x
 *  HMAC-SHA256    371   2115   5.7x    303   2108   6.9x
 *
 * aarch64 Crypto Extensions: validated by cross-build + qemu
 * user-mode run (all vectors pass); real-hardware numbers were
 * not measured here (qemu does not model SIMD parallelism).
 */

#ifndef _PLUGIN_SECURE_H_
#define _PLUGIN_SECURE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* install the best available hardware backend into the isecure
 * callback slots. call once at startup, before any crypto.
 * returns 0 on success, -1 if the CPU lacks the required
 * instructions (software stays active), -2 if compiled without
 * a backend for this platform/compiler */
int CRYPTO_ACCEL_Install(void);

/* name of the installed backend (static string), NULL when
 * nothing is installed */
const char *CRYPTO_ACCEL_BackendName(void);

#ifdef __cplusplus
}
#endif

#endif /* _PLUGIN_SECURE_H_ */
