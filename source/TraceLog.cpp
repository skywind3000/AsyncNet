#include "TraceLog.h"

#include <stdarg.h>


NAMESPACE_BEGIN(AsyncNet)

//---------------------------------------------------------------------
// Logging：日志输出
//---------------------------------------------------------------------
Trace Trace::Global;
Trace Trace::Null;
Trace Trace::ConsoleWhite(NULL, true, CTEXT_WHITE);
Trace Trace::LogFile("RttTrace_", false, CTEXT_WHITE);
Trace Trace::ConsoleMagenta(NULL, true, CTEXT_BOLD_MAGENTA);
Trace Trace::ConsoleGreen(NULL, true, CTEXT_BOLD_GREEN);

Trace::Trace(const char *prefix, bool STDOUT, int color)
 { 
	_mask = 0; 
	_user = NULL; 
	_buffer = new char [8192];
	_output = NULL; 
	_prefix = NULL;
	_fp = NULL;
	_tmtext = NULL;
	_fntext = NULL;
	_stdout = false;
	_color = -1;
	if (prefix || STDOUT) {
		open(prefix, STDOUT);
	}
	if (color >= 0) {
		this->color(color);
	}
}

Trace::~Trace()
{
	close();
	_mask = 0; 
	_user = NULL; 
	_output = NULL; 
	delete []_buffer;
	_buffer = NULL;
}

void Trace::close()
{
	if (_fp) fclose(_fp);
	if (_prefix) delete []_prefix;
	if (_tmtext) delete []_tmtext;
	if (_fntext) delete []_fntext;
	_fp = NULL;
	_prefix = NULL;
	_tmtext = NULL;
	_fntext = NULL;
	_stdout = false;
	setout(NULL, NULL);
}

void Trace::open(const char *prefix, bool STDOUT)
{
	close();
	if (prefix != NULL) {
		//prefix = "";
		int size = strlen(prefix);
		_prefix = new char [size + 1];
		memcpy(_prefix, prefix, size + 1);
	}
	else {
		if (_prefix) delete _prefix;
		_prefix = NULL;
	}
	_tmtext = new char [64];
	_fntext = new char [1024];
	_saved_date.datetime = 0;
	_fntext[0] = 0;
	_stdout = STDOUT;
	setout(StaticOut, this);
}

void Trace::setout(TraceOut out, void *user)
{
	_output = out;
	_user = user;
}

void Trace::out(int mask, const char *fmt, ...)
{
	if ((mask & _mask) != 0 && _output != NULL) {
		System::CriticalScope scope_lock(_lock);
		va_list argptr;
		va_start(argptr, fmt);
		vsprintf(_buffer, fmt, argptr);
		va_end(argptr);
		_output(_buffer, _user);
	}
}

void Trace::binary(int mask, const void *bin, int size)
{
	static const char hex[17] = "0123456789ABCDEF";
	if ((mask & _mask) != 0 || _output != NULL) {
		System::CriticalScope scope_lock(_lock);
		const unsigned char *src = (const unsigned char*)bin;
		char line[100];
		int count = (size + 15) / 16;
		int offset = 0;
		for (int i = 0; i < count; i++, src += 16, offset += 16) {
			int length = size > 16? 16 : size;
			memset(line, ' ', 99);
			line[99] = 0;
			line[0] = hex[(offset >> 12) & 15];
			line[1] = hex[(offset >>  8) & 15];
			line[2] = hex[(offset >>  4) & 15];
			line[3] = hex[(offset >>  0) & 15];
			for (int j = 0; j < 16 && j < length; j++) {
				int start = 6 + j * 3;
				line[start + 0] = hex[src[j] >> 4];
				line[start + 1] = hex[src[j] & 15];
				if (j == 8) line[start - 1] = '-';
			}
			line[6 + length * 3] = '\0';
			_output(line, _user);
		}
	}
}

void Trace::StaticOut(const char *text, void *user)
{
	Trace *self = (Trace*)user;

	System::DateTime now;

	now.localtime();

	if (now.datetime != self->_saved_date.datetime) {
		self->_saved_date.datetime = now.datetime;
		sprintf(self->_tmtext, "%02d:%02d:%02d:%03d", now.hour(), 
			now.minute(), now.second(), now.millisec());
		int nowday = now.mday() + now.month() * 32;
		if (self->_saved_day != nowday) {
			self->_saved_day = nowday;
			self->_fntext[0] = 0;
		}
	}

	if (self->_prefix) {
		if (self->_fntext[0] == 0) {
			if (self->_fp) fclose(self->_fp);
			self->_fp = NULL;
			sprintf(self->_fntext, "%s%04d%02d%02d.log", 
				self->_prefix, now.year(), now.month(), now.mday());
			self->_fp = fopen(self->_fntext, "a");
			if (self->_fp) {
				fseek(self->_fp, 0, SEEK_END);
			}
		}

		if (self->_fp) {
			fprintf(self->_fp, "[%s] %s\n", self->_tmtext, text);
            fflush(self->_fp);
		}
	}

	if (self->_stdout) {
		if (self->_color >= 0) {
			console_set_color(self->_color);
		}
		printf("[%s] %s\n", self->_tmtext, text);
		if (self->_color >= 0) {
			console_reset();
		}
		fflush(stdout);
	}
}


// 设置颜色，只用于控制台输出(open时 STDOUT=true)，高四位为背景色，低四位为前景色
// 色彩编码见：http://en.wikipedia.org/wiki/ANSI_escape_code
int Trace::color(int color)
{
	int old = _color;
	_color = color;
	return old;
}


NAMESPACE_END(AsyncNet)

