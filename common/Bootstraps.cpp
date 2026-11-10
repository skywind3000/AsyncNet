//=====================================================================
//
// Bootstraps.cpp - 
//
// Last Modified: 2026/11/07 15:21:03
//
//=====================================================================
#include "Bootstraps.h"

#include "AsyncEvt.h"
#include "AsyncKit.h"
#include "ExecutorLib.h"

#include "../module/plugin/plugin_stacktrace.h"
#include "../module/plugin/plugin_secure.h"

#include <mutex>

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
// #include "WepollDriver.h"
#endif


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// 内部初始化函数，分阶段初始化系统模块和插件
//---------------------------------------------------------------------
static void InitializeStage0();
static void InitializeStage1();


//---------------------------------------------------------------------
// 初始化：在调用任何模块之前调用，确保系统模块和插件被正确初始化
// 可以重复调用，内部使用 std::call_once 确保只初始化一次
//---------------------------------------------------------------------
int InitializeBootstrap()
{
	static std::once_flag inited;
	int code = 0;
	std::call_once(inited, [&code]() {
		InitializeStage0();
		InitializeStage1();
		code = 1;
	});
	// printf("bootstrap\n");
	return code;
}


//---------------------------------------------------------------------
// initialize modules in the system folder
//---------------------------------------------------------------------
static void InitializeStage0()
{
	iposix_bootstrap();
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	// WepollDriver_Install();
#endif
	ipoll_init(IDEVICE_AUTO);
	iposix_clock_nanosec(IPOSIX_CLOCK_REALTIME);
}


//---------------------------------------------------------------------
// initialize plugins
//---------------------------------------------------------------------
static void InitializeStage1()
{
	plugin_stacktrace_install();
	// CRYPTO_ACCEL_Install();
}


//---------------------------------------------------------------------
// 自动初始化
//---------------------------------------------------------------------

// 通用方式：先有个通用全局对象，构造函数调用 InitializeBootstrap
struct CAutoBootstrap { inline CAutoBootstrap() { InitializeBootstrap(); } };
CAutoBootstrap g_AutoBootstrap;


// 专用方式：尝试尽可能早的调用一次 InitializeBootstrap()
#if defined(__GNUC__)
__attribute__((constructor(101))) static void AutoBootstrap() { InitializeBootstrap(); }
#else
#endif




//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



