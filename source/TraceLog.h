#ifndef __TRACELOG_H__
#define __TRACELOG_H__

#include "../system/system.h"

NAMESPACE_BEGIN(AsyncNet)

//---------------------------------------------------------------------
// 日志输出
//---------------------------------------------------------------------
class Trace
{
public:
	Trace(const char *prefix = NULL, bool STDOUT = false, int color = -1);
	virtual ~Trace();

	typedef void (*TraceOut)(const char *text, void *user);

	bool available(int mask) const { return ((_mask & mask) && _output); }

	void setmask(int mask) { _mask = mask; }
	void enable(int mask) { _mask |= mask; }
	void disable(int mask) { _mask &= ~mask; }

	void setout(TraceOut out, void *user);
	void out(int mask, const char *fmt, ...);
	void binary(int mask, const void *bin, int size);

	// 如果 prefix == NULL则不向文件输出
	void open(const char *prefix, bool STDOUT = false);
	void close();

	// 设置颜色，只用于控制台输出(open时 STDOUT=true)，高四位为背景色，低四位为前景色
	// 色彩编码见：http://en.wikipedia.org/wiki/ANSI_escape_code，返回先前颜色
	int color(int color = -1);

	static Trace Global;
	static Trace Null;
	static Trace ConsoleWhite;
    static Trace LogFile;
	static Trace ConsoleMagenta;
	static Trace ConsoleGreen;

protected:
	static void StaticOut(const char *text, void *user);

protected:
	TraceOut _output;
	System::DateTime _saved_date;
	void *_user;
	char *_buffer;
	char *_prefix;
	bool _stdout;
	int _saved_day;
	FILE *_fp;
	char *_tmtext;
	char *_fntext;
	int _color;
	System::CriticalSection _lock;
	int _mask;
};


#define TRACE_ERROR				1
#define TRACE_WARNING			2
#define TRACE_MGR_PACKET		4
#define TRACE_MGR_SYN			8
#define TRACE_MGR_EVENT			16
#define TRACE_SESSION			32
#define TRACE_KCP				64
#define TRACE_SERVER			128
#define TRACE_CLIENT			256
#define TRACE_UDP_BASIC			512
#define TRACE_UDP_BYTES			1024
#define TRACE_UDP_ERROR			2048
#define TRACE_RTT_REPORT        8192


NAMESPACE_END(AsyncNet)


#endif

