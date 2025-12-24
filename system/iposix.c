//=====================================================================
//
// iposix.c - posix file system accessing
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================

#include "iposix.h"

#ifndef IDISABLE_FILE_SYSTEM_ACCESS
//---------------------------------------------------------------------
// Global Definition
//---------------------------------------------------------------------
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/sysctl.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define ISYSNAME 'w'
#else
#ifndef __unix
#define __unix
#endif
#define ISYSNAME 'u'
#endif



//---------------------------------------------------------------------
// Posix Stat
//---------------------------------------------------------------------
#ifdef __unix
typedef struct stat iposix_ostat_t;
#define iposix_stat_proc	stat
#define iposix_lstat_proc	lstat
#define iposix_fstat_proc	fstat
#else
typedef struct _stat iposix_ostat_t;
#define iposix_stat_proc	_stat
#define iposix_wstat_proc	_wstat
#define iposix_lstat_proc	_stat
#define iposix_fstat_proc	_fstat
#endif


#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
	#if defined(_S_IFMT) && (!defined(S_IFMT))
		#define S_IFMT _S_IFMT
	#endif

	#if defined(_S_IFDIR) && (!defined(S_IFDIR))
		#define S_IFDIR _S_IFDIR
	#endif

	#if defined(_S_IFCHR) && (!defined(S_IFCHR))
		#define S_IFCHR _S_IFCHR
	#endif

	#if defined(_S_IFIFO) && (!defined(S_IFIFO))
		#define S_IFIFO _S_IFIFO
	#endif

	#if defined(_S_IFREG) && (!defined(S_IFREG))
		#define S_IFREG _S_IFREG
	#endif

	#if defined(_S_IREAD) && (!defined(S_IREAD))
		#define S_IREAD _S_IREAD
	#endif

	#if defined(_S_IWRITE) && (!defined(S_IWRITE))
		#define S_IWRITE _S_IWRITE
	#endif

	#if defined(_S_IEXEC) && (!defined(S_IEXEC))
		#define S_IEXEC _S_IEXEC
	#endif
#endif

#define IX_FMT(m, t)  (((m) & S_IFMT) == (t))


// convert stat structure
void iposix_stat_convert(iposix_stat_t *ostat, const iposix_ostat_t *x)
{
	memset(ostat, 0, sizeof(iposix_stat_t));
	ostat->st_mode = 0;

	#ifdef S_IFDIR
	if (IX_FMT(x->st_mode, S_IFDIR)) ostat->st_mode |= ISTAT_IFDIR;
	#endif
	#ifdef S_IFCHR
	if (IX_FMT(x->st_mode, S_IFCHR)) ostat->st_mode |= ISTAT_IFCHR;
	#endif
	#ifdef S_IFBLK
	if (IX_FMT(x->st_mode, S_IFBLK)) ostat->st_mode |= ISTAT_IFBLK;
	#endif
	#ifdef S_IFREG
	if (IX_FMT(x->st_mode, S_IFREG)) ostat->st_mode |= ISTAT_IFREG;
	#endif
	#ifdef S_IFIFO
	if (IX_FMT(x->st_mode, S_IFIFO)) ostat->st_mode |= ISTAT_IFIFO;
	#endif
	#ifdef S_IFLNK
	if (IX_FMT(x->st_mode, S_IFLNK)) ostat->st_mode |= ISTAT_IFLNK;
	#endif
	#ifdef S_IFSOCK
	if (IX_FMT(x->st_mode, S_IFSOCK)) ostat->st_mode |= ISTAT_IFSOCK;
	#endif
	#ifdef S_IFWHT
	if (IX_FMT(x->st_mode, S_IFWHT)) ostat->st_mode |= ISTAT_IFWHT;
	#endif

#ifdef S_IREAD
	if (x->st_mode & S_IREAD) ostat->st_mode |= ISTAT_IRUSR;
#endif

#ifdef S_IWRITE
	if (x->st_mode & S_IWRITE) ostat->st_mode |= ISTAT_IWUSR;
#endif

#ifdef S_IEXEC
	if (x->st_mode & S_IEXEC) ostat->st_mode |= ISTAT_IXUSR;
#endif

#ifdef S_IRUSR
	if (x->st_mode & S_IRUSR) ostat->st_mode |= ISTAT_IRUSR;
	if (x->st_mode & S_IWUSR) ostat->st_mode |= ISTAT_IWUSR;
	if (x->st_mode & S_IXUSR) ostat->st_mode |= ISTAT_IXUSR;
#endif

#ifdef S_IRGRP
	if (x->st_mode & S_IRGRP) ostat->st_mode |= ISTAT_IRGRP;
	if (x->st_mode & S_IWGRP) ostat->st_mode |= ISTAT_IWGRP;
	if (x->st_mode & S_IXGRP) ostat->st_mode |= ISTAT_IXGRP;
#endif

#ifdef S_IROTH
	if (x->st_mode & S_IROTH) ostat->st_mode |= ISTAT_IROTH;
	if (x->st_mode & S_IWOTH) ostat->st_mode |= ISTAT_IWOTH;
	if (x->st_mode & S_IXOTH) ostat->st_mode |= ISTAT_IXOTH;
#endif
	
	ostat->st_size = (IUINT64)x->st_size;

	ostat->atime = (IUINT64)x->st_atime;
	ostat->mtime = (IUINT64)x->st_mtime;
	ostat->ctime = (IUINT64)x->st_ctime;

	ostat->st_ino = (IUINT64)x->st_ino;
	ostat->st_dev = (IUINT64)x->st_dev;
	ostat->st_nlink = (IUINT32)x->st_nlink;
	ostat->st_uid = (IUINT32)x->st_uid;
	ostat->st_gid = (IUINT32)x->st_gid;
	ostat->st_rdev = (IUINT32)x->st_rdev;

#ifdef __unix
//	#define IHAVE_STAT_ST_BLKSIZE
//	#define IHAVE_STAT_ST_BLOCKS
//	#define IHAVE_STAT_ST_FLAGS
#endif

#if defined(__unix)
	#ifdef IHAVE_STAT_ST_BLOCKS
	ostat->st_blocks = (IUINT32)x->st_blocks;
	#endif
	#ifdef IHAVE_STAT_ST_BLKSIZE
	ostat->st_blksize = (IUINT32)x->st_blksize;
	#endif
	#if !defined(__CYGWIN__) && defined(IHAVE_STAT_ST_FLAGS)
	ostat->st_flags = (IUINT32)x->st_flags;
	#endif
#endif
}

// returns 0 for success, -1 for error
int iposix_stat_imp(const char *path, iposix_stat_t *ostat)
{
	iposix_ostat_t xstat;
	int retval;
	retval = iposix_stat_proc(path, &xstat);
	if (retval != 0) return -1;
	iposix_stat_convert(ostat, &xstat);
	return 0;
}

// returns 0 for success, -1 for error
int iposix_wstat_imp(const wchar_t *path, iposix_stat_t *ostat)
{
#ifdef _WIN32
	iposix_ostat_t xstat;
	int retval;
	retval = iposix_wstat_proc(path, &xstat);
	if (retval != 0) return -1;
	iposix_stat_convert(ostat, &xstat);
	return 0;
#else
	int retval;
	char *buf = malloc(IPOSIX_MAXPATH * 4 + 8);
	if (buf == NULL) return -1;
	wcstombs(buf, path, IPOSIX_MAXPATH * 4 + 4);
	retval = iposix_stat_imp(buf, ostat);
	free(buf);
	return retval;
#endif
}

// returns 0 for success, -1 for error
int iposix_lstat_imp(const char *path, iposix_stat_t *ostat)
{
	iposix_ostat_t xstat;
	int retval;
	retval = iposix_lstat_proc(path, &xstat);
	if (retval != 0) return -1;
	iposix_stat_convert(ostat, &xstat);
	return 0;
}

// returns 0 for success, -1 for error
int iposix_fstat(int fd, iposix_stat_t *ostat)
{
	iposix_ostat_t xstat;
	int retval;
	retval = iposix_fstat_proc(fd, &xstat);
	if (retval != 0) return -1;
	iposix_stat_convert(ostat, &xstat);
	return 0;
}

// normalize stat path
static void iposix_path_stat(const char *src, char *dst)
{
	int size = (int)strlen(src);
	if (size > IPOSIX_MAXPATH) size = IPOSIX_MAXPATH;
	memcpy(dst, src, size + 1);
	if (size > 1) {
		int trim = 1;
		if (size == 3) {
			if (isalpha((int)dst[0]) && dst[1] == ':' && 
				(dst[2] == '/' || dst[2] == '\\')) trim = 0;
		}
		if (size == 1) {
			if (dst[0] == '/' || dst[0] == '\\') trim = 0;
		}
		if (trim) {
			if (dst[size - 1] == '/' || dst[size - 1] == '\\') {
				dst[size - 1] = 0;
				size--;
			}
		}
	}
}

// wide char version
static void iposix_path_wstat(const wchar_t *src, wchar_t *dst)
{
	int size = (int)wcslen(src);
	if (size > IPOSIX_MAXPATH) size = IPOSIX_MAXPATH;
	memcpy(dst, src, (size + 1) * sizeof(wchar_t));
	if (size > 1) {
		int trim = 1;
		if (size == 3) {
			if (isalpha((int)dst[0]) && dst[1] == L':' && 
				(dst[2] == L'/' || dst[2] == L'\\')) trim = 0;
		}
		if (size == 1) {
			if (dst[0] == L'/' || dst[0] == L'\\') trim = 0;
		}
		if (trim) {
			if (dst[size - 1] == L'/' || dst[size - 1] == L'\\') {
				dst[size - 1] = 0;
				size--;
			}
		}
	}
}


// returns 0 for success, -1 for error
int iposix_stat(const char *path, iposix_stat_t *ostat)
{
	char buf[IPOSIX_MAXBUFF];
	iposix_path_stat(path, buf);
	return iposix_stat_imp(buf, ostat);
}

// wide-char: returns 0 for success, -1 for error
int iposix_wstat(const wchar_t *path, iposix_stat_t *ostat)
{
	wchar_t buf[IPOSIX_MAXBUFF];
	iposix_path_wstat(path, buf);
	return iposix_wstat_imp(buf, ostat);
}

// returns 0 for success, -1 for error
int iposix_lstat(const char *path, iposix_stat_t *ostat)
{
	char buf[IPOSIX_MAXBUFF];
	iposix_path_stat(path, buf);
	return iposix_lstat_imp(buf, ostat);
}

// get current directory
char *iposix_getcwd(char *path, int size)
{
#ifdef _WIN32
	return _getcwd(path, size);
#else
	return getcwd(path, size);
#endif
}

// wide-char: get current directory (wide char)
wchar_t *iposix_wgetcwd(wchar_t *path, int size)
{
#ifdef _WIN32
	return _wgetcwd(path, size);
#else
	char buf[IPOSIX_MAXPATH + 8];
	char *ret = getcwd(buf, IPOSIX_MAXPATH);
	if (ret == NULL) return NULL;
	mbstowcs(path, buf, size);
	return path;
#endif
}

// create directory
int iposix_mkdir(const char *path, int mode)
{
#ifdef _WIN32
	return _mkdir(path);
#else
	if (mode < 0) mode = 0755;
	return mkdir(path, mode);
#endif
}

// wide-char: create directory (wide char)
int iposix_wmkdir(const wchar_t *path, int mode)
{
#ifdef _WIN32
	return _wmkdir(path);
#else
	int hr;
	char *buf = (char*)malloc(IPOSIX_MAXBUFF);
	if (buf == NULL) return -1;
	wcstombs(buf, path, IPOSIX_MAXPATH);
	if (mode < 0) mode = 0755;
	hr = mkdir(buf, mode);
	free(buf);
	return hr;
#endif
}

// change directory
int iposix_chdir(const char *path)
{
#ifdef _WIN32
	return _chdir(path);
#else
	return chdir(path);
#endif
}

// wide-char: change directory (wide char)
int iposix_wchdir(const wchar_t *path)
{
#ifdef _WIN32
	return _wchdir(path);
#else
	char buf[IPOSIX_MAXPATH + 8];
	wcstombs(buf, path, IPOSIX_MAXPATH);
	return chdir(buf);
#endif
}

// check access
int iposix_access(const char *path, int mode)
{
#ifdef _WIN32
	return _access(path, mode);
#else
	return access(path, mode);
#endif
}

// wide-char: check access (wide char)
int iposix_waccess(const wchar_t *path, int mode)
{
#ifdef _WIN32
	return _waccess(path, mode);
#else
	char buf[IPOSIX_MAXPATH + 8];
	wcstombs(buf, path, IPOSIX_MAXPATH);
	return access(buf, mode);
#endif
}

// returns 1 for true 0 for false, -1 for not exist
int iposix_path_isdir(const char *path)
{
	iposix_stat_t s;
	int hr = iposix_stat(path, &s);
	if (hr != 0) return -1;
	return (ISTAT_ISDIR(s.st_mode))? 1 : 0;
}

// wide-char: returns 1 for true 0 for false, -1 for not exist
int iposix_path_wisdir(const wchar_t *path)
{
	iposix_stat_t s;
	int hr = iposix_wstat(path, &s);
	if (hr != 0) return -1;
	return (ISTAT_ISDIR(s.st_mode))? 1 : 0;
}

// returns 1 for true 0 for false, -1 for not exist
int iposix_path_isfile(const char *path)
{
	iposix_stat_t s;
	int hr = iposix_stat(path, &s);
	if (hr != 0) return -1;
	return (ISTAT_ISDIR(s.st_mode))? 0 : 1;
}

// wide-char: returns 1 for true 0 for false, -1 for not exist
int iposix_path_wisfile(const wchar_t *path)
{
	iposix_stat_t s;
	int hr = iposix_wstat(path, &s);
	if (hr != 0) return -1;
	return (ISTAT_ISDIR(s.st_mode))? 0 : 1;
}

// returns 1 for true 0 for false, -1 for not exist
int iposix_path_islink(const char *path)
{
	iposix_stat_t s;
	int hr = iposix_stat(path, &s);
	if (hr != 0) return -1;
	return (ISTAT_ISLNK(s.st_mode))? 1 : 0;
}

// wide-char: returns 1 for true 0 for false, -1 for not exist
int iposix_path_wislink(const wchar_t *path)
{
	iposix_stat_t s;
	int hr = iposix_wstat(path, &s);
	if (hr != 0) return -1;
	return (ISTAT_ISLNK(s.st_mode))? 1 : 0;
}

// returns 1 for true 0 for false
int iposix_path_exists(const char *path)
{
	iposix_stat_t s;
	int hr = iposix_stat(path, &s);
	if (hr != 0) return 0;
	return 1;
}

// wide-char: returns 1 for true 0 for false
int iposix_path_wexists(const wchar_t *path)
{
	iposix_stat_t s;
	int hr = iposix_wstat(path, &s);
	if (hr != 0) return 0;
	return 1;
}

// returns file size, -1 for error
IINT64 iposix_path_getsize(const char *path)
{
	iposix_stat_t s;
	int hr = iposix_stat(path, &s);
	if (hr != 0) return -1;
	return (IINT64)s.st_size;
}

// returns file size, -1 for error
IINT64 iposix_path_wgetsize(const wchar_t *path)
{
	iposix_stat_t s;
	int hr = iposix_wstat(path, &s);
	if (hr != 0) return -1;
	return (IINT64)s.st_size;
}



//---------------------------------------------------------------------
// Posix Path
//---------------------------------------------------------------------

// 是否是绝对路径，如果是的话返回1，否则返回0
int iposix_path_isabs(const char *path)
{
	if (path == NULL) return 0;
	if (path[0] == '/') return 1;
	if (path[0] == 0) return 0;
#ifdef _WIN32
	if (path[0] == IPATHSEP) return 1;
	if (isalpha(path[0]) && path[1] == ':') {
		if (path[2] == '/' || path[2] == '\\') return 1;
	}
#endif
	return 0;
}

// wide-char: check absolute path, returns 1 for true 0 for false
int iposix_path_wisabs(const wchar_t *path)
{
	if (path == NULL) return 0;
	if (path[0] == L'/') return 1;
	if (path[0] == 0) return 0;
#ifdef _WIN32
	if (path[0] == IPATHSEP) return 1;
	if (isalpha((int)path[0]) && path[1] == L':') {
		if (path[2] == L'/' || path[2] == L'\\') return 1;
	}
#endif
	return 0;
}


//---------------------------------------------------------------------
// iposix_str_t - basic string definition
//---------------------------------------------------------------------
typedef struct {
	char *p;
	int l;
	int m;
}	iposix_str_t;


//---------------------------------------------------------------------
// iposix_str_t interface
//---------------------------------------------------------------------
#define _istrlen(s) ((s)->l)
#define iposix_str_charh(s, i) (((i) >= 0)? ((s)->p)[i] : ((s)->p)[(s)->l + (i)])

static char *iposix_str_init(iposix_str_t *s, char *p, int max)
{
	assert((max > 0) && p && s);
	s->p = p;
	s->l = 0;
	s->m = max;
	return p;
}

char *iposix_str_set(iposix_str_t *s, const char *p, int max)
{
	assert((max > 0) && p && s);
	s->p = (char*)p;
	s->l = (int)strlen(p);
	s->m = max;
	return (char*)p;
}

static char *iposix_str_cat(iposix_str_t *s, const char *p) 
{
	char *p1;

	assert(s && p);
	for (p1 = (char*)p; p1[0]; p1++, s->l++) {
		if (s->l >= s->m) break;
		s->p[s->l] = p1[0];
	}
	return s->p;
}

static char *iposix_str_copy(iposix_str_t *s, const char *p) 
{
	assert(s && p);
	s->l = 0;
	return iposix_str_cat(s, p);
}

static char *iposix_str_cats(iposix_str_t *s1, const iposix_str_t *s2) 
{
	int i;
	assert(s1 && s2);
	for (i = 0; i < s2->l; i++, s1->l++) {
		if (s1->l >= s1->m) break;
		s1->p[s1->l] = s2->p[i];
	}
	return s1->p;
}

static char *iposix_str_cstr(iposix_str_t *s) 
{
	assert(s);
	if (s->l >= s->m) s->l = s->m - 1;
	if (s->l < 0) s->l = 0;
	s->p[s->l] = 0;
	return s->p;
}

static char iposix_str_char(const iposix_str_t *s, int pos)
{
	if (pos >= 0) return (pos > s->l)? 0 : s->p[pos];
	return (pos < -(s->l))? 0 : s->p[s->l + pos];
}

static char iposix_str_charhop(iposix_str_t *s)
{
	char ch = iposix_str_char(s, -1);
	s->l--;
	if (s->l < 0) s->l = 0;
	return ch;
}

static char *iposix_str_chartok(iposix_str_t *s, const char *p)
{
	int i, k;

	assert(s && p);

	for (; _istrlen(s) > 0; ) {
		for (i = 0, k = 0; p[i] && k == 0; i++) {
			if (iposix_str_char(s, -1) == p[i]) k++;
		}
		if (k == 0) break;
		iposix_str_charhop(s);
	}
	for (; _istrlen(s) > 0; ) {
		for (i = 0, k = 0; p[i] && k == 0; i++) {
			if (iposix_str_char(s, -1) == p[i]) k++;
		}
		if (k) break;
		iposix_str_charhop(s);
	}

	return s->p;
}

static int iposix_str_charmp(iposix_str_t *s, const char *p)
{
	int i;
	for (i = 0; i < s->l && ((char*)p)[i]; i++)
		if (iposix_str_char(s, i) != ((char*)p)[i]) break;
	if (((char*)p)[i] == 0 && i == s->l) return 0;
	return 1;
}

static char *iposix_str_catc(iposix_str_t *s, char ch)
{
	char text[2] = " ";
	assert(s);
	text[0] = ch;
	return iposix_str_cat(s, text);
}

static int iposix_str_strtok(const char *p1, int *pos, const char *p2)
{
	int i, j, k, r;

	assert(p1 && pos && p2);

	for (i = *pos; p1[i]; i++) {
		for (j = 0, k = 0; p2[j] && k == 0; j++) {
			if (p1[i] == p2[j]) k++;
		}
		if (k == 0) break;
	}
	*pos = i;
	r = i;

	if (p1[i] == 0) return -1;
	for (; p1[i]; i++) {
		for (j = 0, k = 0; p2[j] && k == 0; j++) {
			if (p1[i] == p2[j]) k++;
		}
		if (k) break;
	}
	*pos = i;

	return r;
}


//---------------------------------------------------------------------
// normalize path
//---------------------------------------------------------------------
char *iposix_path_normal(const char *srcpath, char *path, int maxsize)
{
	int i, p, c, k, r, t = 0;
	iposix_str_t s1, s2;
	char *p1, *p2;
	char pp2[3];

	assert(srcpath && path && maxsize > 0);

	if (srcpath[0] == 0) {
		if (maxsize > 0) path[0] = 0;
		if (maxsize > 1) {
			path[0] = '.';
			path[1] = 0;
		}
		return path;
	}

	p1 = (char*)srcpath;

	path[0] = 0;
	iposix_str_init(&s1, path, maxsize);

	if (IPATHSEP == '\\') {
		pp2[0] = '/';
		pp2[1] = '\\';
		pp2[2] = 0;
	}	else {
		pp2[0] = '/';
		pp2[1] = 0;
	}

	p2 = pp2;

	if (p1[0] && p1[1] == ':' && (ISYSNAME == 'u' || ISYSNAME == 'w')) {
		iposix_str_catc(&s1, *p1++);
		iposix_str_catc(&s1, *p1++);
	}

	if (IPATHSEP == '/') {
		if (p1[0] == '/') iposix_str_catc(&s1, *p1++);
	}	
	else if (p1[0] == '/' || p1[0] == IPATHSEP) {
		iposix_str_catc(&s1, IPATHSEP);
		p1++;
	}

	r = (iposix_str_char(&s1, -1) == IPATHSEP)? 1 : 0;
	srcpath = (const char*)p1;	

	for (i = 0, c = 0, k = 0; (p = iposix_str_strtok(srcpath, &i, p2)) >= 0; k++) {
		s2.p = (char*)(srcpath + p);
		s2.l = s2.m = i - p;
		// _iputs(&s2); printf("*\n");
		if (iposix_str_charmp(&s2, ".") == 0) continue;
		if (iposix_str_charmp(&s2, "..") == 0) {
			if (c != 0) {
				iposix_str_chartok(&s1, (IPATHSEP == '\\')? "/\\:" : "/");
				c--;
				continue;
			}
			if (c == 0 && r) {
				continue;
			}
		}	else {
			c++;
		}
		t++;
		iposix_str_cats(&s1, &s2);
		iposix_str_catc(&s1, IPATHSEP);
	}
	if (_istrlen(&s1) == 0) {
		iposix_str_copy(&s1, ".");
	}	else {
		if (iposix_str_char(&s1, -1) == IPATHSEP && t > 0) 
			iposix_str_charhop(&s1);
	}
	return iposix_str_cstr(&s1);
}


//---------------------------------------------------------------------
// join path
//---------------------------------------------------------------------
char *iposix_path_join(const char *p1, const char *p2, char *path, int maxsize)
{
	iposix_str_t s;
	char cc;
	int postsep = 1;
	int len1;

	assert(p1 && p2 && maxsize > 0);

	iposix_str_init(&s, path, maxsize);

	if (p1 == NULL || p1[0] == 0) {
		if (p2 == NULL || p2[0] == 0) {
			if (maxsize > 0) path[0] = 0;
			return path;
		}
		else {
			iposix_str_cat(&s, p2);
			return iposix_str_cstr(&s);
		}
	}
	else if (p2 == NULL || p2[0] == 0) {
		len1 = (int)strlen(p1);
		cc = (len1 > 0)? p1[len1 - 1] : 0;
		if (cc == '/' || cc == '\\') {
			iposix_str_cat(&s, p1);
			return iposix_str_cstr(&s);
		} else {
			iposix_str_cat(&s, p1);
			iposix_str_catc(&s, IPATHSEP);
		}
		return iposix_str_cstr(&s);
	}
	else if (iposix_path_isabs(p2)) {
#ifdef _WIN32
		if (p2[0] == '\\' || p2[0] == '/') {
			if (p1[1] == ':') {
				iposix_str_catc(&s, p1[0]);
				iposix_str_catc(&s, ':');
				iposix_str_cat(&s, p2);
				return iposix_str_cstr(&s);
			}
		}
#endif
		iposix_str_cat(&s, p2);
		return iposix_str_cstr(&s);
	}
	else {
#ifdef _WIN32
		char d1 = (p1[1] == ':')? p1[0] : 0;
		char d2 = (p2[1] == ':')? p2[0] : 0;
		char m1 = (d1 >= 'A' && d1 <= 'Z')? (d1 - 'A' + 'a') : d1;
		char m2 = (d2 >= 'A' && d2 <= 'Z')? (d2 - 'A' + 'a') : d2;
		if (d1 != 0) {
			if (d2 != 0) {
				if (m1 == m2) {
					path[0] = d2;
					path[1] = ':';
					iposix_path_join(p1 + 2, p2 + 2, path + 2, maxsize - 2);
					return path;
				} else {
					iposix_str_cat(&s, p2);
					return iposix_str_cstr(&s);
				}
			}
		}
		else if (d2 != 0) {
			iposix_str_cat(&s, p2);
			return iposix_str_cstr(&s);
		}
#endif
	}

	postsep = 1;
	len1 = (int)strlen(p1);
	cc = (len1 > 0)? p1[len1 - 1] : 0;

	if (cc == '/') {
		postsep = 0;
	}   
	else {
#ifdef _WIN32
		if (cc == '\\') {
			postsep = 0;
		}
		else if (len1 == 2 && p1[1] == ':') {
			postsep = 0;
		}
#endif
	}

	iposix_str_cat(&s, p1);

	if (postsep) {
		iposix_str_catc(&s, IPATHSEP);
	}

	iposix_str_cat(&s, p2);

	return iposix_str_cstr(&s);
}


// 绝对路径
char *iposix_path_abspath_unix(const char *srcpath, char *path, int maxsize)
{
	char *base, *temp;
	base = (char*)malloc(IPOSIX_MAXBUFF * 2);
	temp = base + IPOSIX_MAXBUFF;
	if (base == NULL) return NULL;
	iposix_getcwd(base, IPOSIX_MAXPATH);
	iposix_path_join(base, srcpath, temp, IPOSIX_MAXBUFF);
	iposix_path_normal(temp, path, maxsize);
	free(base);
	return path;
}

#ifdef _WIN32
char *iposix_path_abspath_win(const char *srcpath, char *path, int maxsize)
{
	char *fname;
	DWORD hr = GetFullPathNameA(srcpath, maxsize, path, &fname);
	if (hr == 0) return NULL;
	return path;
}

wchar_t *iposix_path_abspath_wwin(const wchar_t *srcpath, wchar_t *path, int maxsize)
{
	wchar_t *fname;
	DWORD hr = GetFullPathNameW(srcpath, (DWORD)maxsize, path, &fname);
	if (hr == 0) return NULL;
	return path;
}
#endif


// get absolute path
char *iposix_path_abspath(const char *srcpath, char *path, int maxsize)
{
#ifdef _WIN32
	return iposix_path_abspath_win(srcpath, path, maxsize);
#else
	return iposix_path_abspath_unix(srcpath, path, maxsize);
#endif
}

// wide-char: get absolute path
wchar_t *iposix_path_wabspath(const wchar_t *srcpath, wchar_t *path, int maxsize)
{
#ifdef _WIN32
	return iposix_path_abspath_wwin(srcpath, path, maxsize);
#else
	char temp[IPOSIX_MAXBUFF];
	char *ret;
	wcstombs(temp, srcpath, IPOSIX_MAXBUFF);
	ret = iposix_path_abspath_unix(temp, temp, IPOSIX_MAXBUFF);
	if (ret == NULL) {
		if (maxsize > 0) path[0] = 0;
		return NULL;
	}
	mbstowcs(path, temp, maxsize);
	return path;
#endif
}


//---------------------------------------------------------------------
// UTF-8 to UTF-16
//---------------------------------------------------------------------
int iposix_utf8towc(wchar_t *dest, const char *src, int n) 
{
#ifdef _WIN32
	int required = 0, result = 0;
	if (src == NULL || (src && src[0] == 0)) {
		if (dest && n > 0) dest[0] = L'\0';
		return 0;
	}

	required = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);

	if (required == 0) return -1;
	if (dest == NULL) return required - 1;
	if (n == 0) return 0;

	result = MultiByteToWideChar(CP_UTF8, 0, src, -1, dest, (int)n);
	if (result == 0) {
		if (n > 0) {
			dest[0] = L'\0';
		}
		return -1;
	}
	return result - 1;
#else
	int result = (int)mbstowcs(dest, src, n);
	return result;
#endif
}


//---------------------------------------------------------------------
// UTF-16 to UTF-8
//---------------------------------------------------------------------
int iposix_wcstoutf8(char *dest, const wchar_t *src, int n) {
#ifdef _WIN32
	int required, result = 0;

	if (src == NULL || (src && src[0] == 0)) {
		if (dest && n > 0) dest[0] = '\0';
		return 0;
	}

	required = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);

	if (required == 0) return -1;
	if (dest == NULL) return required - 1;
	if (n == 0) return 0;

	result = WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, (int)n, NULL, NULL);
	if (result == 0) {
		if (n > 0) dest[0] = '\0';
		return -1;
	}

	return result - 1;
#else
	int result = (int)wcstombs(dest, src, n);
	return result;
#endif
}


//---------------------------------------------------------------------
// wide-char: normalize: remove redundant "./", "../" and duplicate separators
//---------------------------------------------------------------------
wchar_t *iposix_path_wnormal(const wchar_t *srcpath, wchar_t *path, int maxsize)
{
	char buf[IPOSIX_MAXBUFF * 2 + 8];
	char *tmp = buf + IPOSIX_MAXBUFF + 4;
	iposix_wcstoutf8(buf, srcpath, IPOSIX_MAXBUFF);
	iposix_path_normal(buf, tmp, IPOSIX_MAXBUFF);
	iposix_utf8towc(path, tmp, maxsize);
	return path;
}


//---------------------------------------------------------------------
// wide-char: concatenate two paths
//---------------------------------------------------------------------
wchar_t *iposix_path_wjoin(const wchar_t *p1, const wchar_t *p2, 
	wchar_t *path, int len)
{
	char buf1[IPOSIX_MAXBUFF + 8];
	char buf2[IPOSIX_MAXBUFF + 8];
	char tmp[IPOSIX_MAXBUFF + 8];
	iposix_wcstoutf8(buf1, p1, IPOSIX_MAXBUFF);
	iposix_wcstoutf8(buf2, p2, IPOSIX_MAXBUFF);
	iposix_path_join(buf1, buf2, tmp, IPOSIX_MAXBUFF);
	iposix_utf8towc(path, tmp, len);
	return path;
}

// get directory name from path
char *iposix_path_dirname(const char *path, char *dir, int maxsize)
{
	iposix_path_split(path, dir, maxsize, NULL, 0);
	return dir;
}

// wide-char: get directory name from path
wchar_t *iposix_path_wdirname(const wchar_t *path, wchar_t *dir, int maxsize)
{
	iposix_path_wsplit(path, dir, maxsize, NULL, 0);
	return dir;
}

// get file name from path
char *iposix_path_basename(const char *path, char *file, int maxsize)
{
	iposix_path_split(path, NULL, 0, file, maxsize);
	return file;
}

// wide-char: get file name from path
wchar_t *iposix_path_wbasename(const wchar_t *path, wchar_t *file, int maxsize)
{
	iposix_path_wsplit(path, NULL, 0, file, maxsize);
	return file;
}


//---------------------------------------------------------------------
// path split: find the last "/" from right to left, split into two parts
//---------------------------------------------------------------------
int iposix_path_split(const char *path, char *p1, int l1, char *p2, int l2)
{
	int length, i, k, root;
	length = (int)strlen(path);

	if (length == 0) {
		if (p1 && l1 > 0) p1[0] = 0;
		if (p2 && l2 > 0) p2[0] = 0;
		return 0;
	}

	for (i = length - 1; i >= 0; i--) {
		if (IPATHSEP == '/') {
			if (path[i] == '/') break;
		}	else {
			if (path[i] == '/' || path[i] == '\\') break;
			if (path[i] == ':') break;
		}
	}

	if (IPATHSEP == '/') {
		root = (i == 0) ? 1 : 0;
	}	else {
		if (i == 0) root = 1;
		else if (i == 2 && path[1] == ':') root = 1;
		else if (i == 1 && path[1] == ':') root = 1;
		else root = 0;
	}

	if (p1) {
		if (i < 0) {
			if (l1 > 0) p1[0] = 0;
		}	
		else if (root) {
			int size = (i + 1) < l1 ? (i + 1) : l1;
			memcpy(p1, path, size);
			if (size < l1) p1[size] = 0;
		}
		else {
			int size = i < l1 ? i : l1;
			memcpy(p1, path, size);
			if (size < l1) p1[size] = 0;
		}
	}

	k = length - i - 1;

	if (p2) {
		if (k <= 0) {
			if (l2 > 0) p2[0] = 0;
		}	else {
			int size = k < l2 ? k : l2;
			memcpy(p2, path + i + 1, size);
			if (size < l2) p2[size] = 0;
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// wide-char: path split: find the last "/" from right to left, split into two parts
//---------------------------------------------------------------------
int iposix_path_wsplit(const wchar_t *path, wchar_t *p1, int l1, wchar_t *p2, int l2)
{
	int length, i, k, root;
	length = (int)wcslen(path);

	if (length == 0) {
		if (p1 && l1 > 0) p1[0] = 0;
		if (p2 && l2 > 0) p2[0] = 0;
		return 0;
	}

	for (i = length - 1; i >= 0; i--) {
		if (IPATHSEP == '/') {
			if (path[i] == L'/') break;
		}	else {
			if (path[i] == L'/' || path[i] == L'\\') break;
		}
	}

	if (IPATHSEP == '/') {
		root = (i == 0) ? 1 : 0;
	}	else {
		if (i == 0) root = 1;
		else if (i == 2 && path[1] == L':') root = 1;
		else if (i == 1 && path[1] == L':') root = 1;
		else root = 0;
	}

	if (p1) {
		if (i < 0) {
			if (l1 > 0) p1[0] = 0;
		}	
		else if (root) {
			int size = (i + 1) < l1 ? (i + 1) : l1;
			memcpy(p1, path, size * sizeof(wchar_t));
			if (size < l1) p1[size] = 0;
		}
		else {
			int size = i < l1 ? i : l1;
			memcpy(p1, path, size * sizeof(wchar_t));
			if (size < l1) p1[size] = 0;
		}
	}

	k = length - i - 1;

	if (p2) {
		if (k <= 0) {
			if (l2 > 0) p2[0] = 0;
		}	else {
			int size = k < l2 ? k : l2;
			memcpy(p2, path + i + 1, size * sizeof(wchar_t));
			if (size < l2) p2[size] = 0;
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// extension split: split file name and extension
//---------------------------------------------------------------------
int iposix_path_splitext(const char *path, char *p1, int l1, char *p2, int l2)
{
	int length, i, k, size, sep;
	length = (int)strlen(path);

	if (length == 0) {
		if (p1 && l1 > 0) p1[0] = 0;
		if (p2 && l2 > 0) p2[0] = 0;
		return 0;
	}

	for (i = length - 1, k = length; i >= 0; i--) {
		if (IPATHSEP == '/') {
			if (path[i] == '/') break;
		}	else {
			if (path[i] == '/' || path[i] == '\\') break;
			if (path[i] == ':') break;
		}
		if (k == length && path[i] == '.') {
			k = i;
		}
	}

	sep = i;

	if (k <= sep) k = length;
	else {
		for (i = sep + 1; i < k; i++) {
			if (path[i] != '.') break;
		}
		if (i == k) {
			k = length;
		}
	}

	if (p1) {
		size = k < l1 ? k : l1;
		if (size > 0) memcpy(p1, path, size);
		if (size < l1) p1[size] = 0;
	}

	size = length - k;
	if (size < 0) size = 0;
	size = size < l2 ? size : l2;

	if (p2) {
		if (size > 0) memcpy(p2, path + k, size);
		if (size < l2) p2[size] = 0;
	}

	return 0;
}


//---------------------------------------------------------------------
// extension split: split file name and extension
//---------------------------------------------------------------------
int iposix_path_wsplitext(const wchar_t *path, wchar_t *p1, int l1, wchar_t *p2, int l2)
{
	int length, i, k, size, sep;
	length = (int)wcslen(path);

	if (length == 0) {
		if (p1 && l1 > 0) p1[0] = 0;
		if (p2 && l2 > 0) p2[0] = 0;
		return 0;
	}

	for (i = length - 1, k = length; i >= 0; i--) {
		if (IPATHSEP == L'/') {
			if (path[i] == L'/') break;
		}	else {
			if (path[i] == L'/' || path[i] == L'\\') break;
			if (path[i] == L':') break;
		}
		if (k == length && path[i] == L'.') {
			k = i;
		}
	}

	sep = i;

	if (k <= sep) k = length;
	else {
		for (i = sep + 1; i < k; i++) {
			if (path[i] != L'.') break;
		}
		if (i == k) {
			k = length;
		}
	}

	if (p1) {
		size = k < l1 ? k : l1;
		if (size > 0) memcpy(p1, path, size * sizeof(wchar_t));
		if (size < l1) p1[size] = 0;
	}

	size = length - k;
	if (size < 0) size = 0;
	size = size < l2 ? size : l2;

	if (p2) {
		if (size > 0) memcpy(p2, path + k, size * sizeof(wchar_t));
		if (size < l2) p2[size] = 0;
	}

	return 0;
}


//---------------------------------------------------------------------
// get file extension from path
//---------------------------------------------------------------------
char *iposix_path_extname(const char *path, char *ext, int maxsize)
{
	iposix_path_splitext(path, NULL, 0, ext, maxsize);
	return ext;
}


//---------------------------------------------------------------------
// wide-char: get file extension from path
//---------------------------------------------------------------------
wchar_t *iposix_path_wextname(const wchar_t *path, wchar_t *ext, int maxsize)
{
	iposix_path_wsplitext(path, NULL, 0, ext, maxsize);
	return ext;
}


// path case normalize (to lower case on Windows)
char *iposix_path_normcase(const char *srcpath, char *path, int maxsize)
{
	int size = (int)strlen(srcpath);
	int i;
	for (i = 0; i < size && i < maxsize - 1; i++) {
#ifdef _WIN32
		char ch = srcpath[i];
		if (ch >= 'A' && ch <= 'Z') 
			ch = (char)(ch - 'A' + 'a');
		path[i] = ch;
#else
		path[i] = srcpath[i];
#endif
	}
	if (i < maxsize) {
		path[i] = 0;
	}
	return path;
}


// wide-char: path case normalize (to lower case on Windows)
wchar_t *iposix_path_wnormcase(const wchar_t *srcpath, wchar_t *path, int maxsize)
{
	int size = (int)wcslen(srcpath);
	int i;
	for (i = 0; i < size && i < maxsize - 1; i++) {
#ifdef _WIN32
		wchar_t ch = srcpath[i];
		if (ch >= L'A' && ch <= L'Z')
			ch = (wchar_t)(ch - L'A' + L'a');
		path[i] = ch;
#else
		path[i] = srcpath[i];
#endif
	}
	if (i < maxsize) {
		path[i] = 0;
	}
	return path;
}


//---------------------------------------------------------------------
// common path, aka. longest common prefix, from two paths
//---------------------------------------------------------------------
char *iposix_path_common(const char *p1, const char *p2, char *path, int maxsize)
{
	int size1, size2, length, i, k = 0;
	size1 = (int)strlen(p1);
	size2 = (int)strlen(p2);
	length = (size1 < size2) ? size1 : size2;

	for (i = 0; i < length; i++) {
		char ch1 = p1[i];
		char ch2 = p2[i];
#ifdef _WIN32
		if (ch1 >= 'A' && ch1 <= 'Z') ch1 = (char)(ch1 - 'A' + 'a');
		if (ch2 >= 'A' && ch2 <= 'Z') ch2 = (char)(ch2 - 'A' + 'a');
		if (ch1 == '\\') ch1 = '/';
		if (ch2 == '\\') ch2 = '/';
#endif
		if (ch1 == '/') {
			if (ch2 == '/') {
				k = i;
			}
			else {
				break;
			}
		}
#ifdef _WIN32
		else if (ch1 == ':') {
			if (ch2 == ':') {
				k = i + 1;
			}	else {
				break;
			}
		}
#endif
		else if (ch1 != ch2) {
			break;
		}
	}

	if (i == length) {
		if (size1 == size2) k = length;
		else if (size1 < size2) {
			if (p2[length] == '/' || p2[length] == '\\') k = length;
		}
		else if (size1 > size2) {
			if (p1[length] == '/' || p1[length] == '\\') k = length;
		}
	}

	if (length > 0) {
		if (k == 0) {
			if (p1[0] == '/' || p1[0] == '\\') {
				if (p2[0] == '/' || p2[0] == '\\') {
					k = 1;
				}
			}
		}
#ifdef _WIN32
		if (k == 2 && length > 3) {
			if (p1[1] == ':' && p2[1] == ':') {
				if (p1[2] == '/' || p1[2] == '\\') {
					if (p2[2] == '/' || p2[2] == '\\') {
						k = 3;
					}
				}
			}
		}
#endif
	}

	if (k > 0) {
		if (k >= maxsize) k = maxsize - 1;
		memcpy(path, p1, k);
		path[k] = 0;
	}	else {
		if (maxsize > 0) path[0] = 0;
	}

	return path;
}


//---------------------------------------------------------------------
// wide-char: common path, aka. longest common prefix, from two paths
//---------------------------------------------------------------------
wchar_t *iposix_path_wcommon(const wchar_t *p1, const wchar_t *p2, wchar_t *path, int maxsize)
{
	char buf[IPOSIX_MAXBUFF * 3 + 12];
	char *tmp = buf + IPOSIX_MAXBUFF + 4;
	char *out = tmp + IPOSIX_MAXBUFF + 4;
	iposix_wcstoutf8(buf, p1, IPOSIX_MAXBUFF);
	iposix_wcstoutf8(tmp, p2, IPOSIX_MAXBUFF);
	iposix_path_common(buf, tmp, out, IPOSIX_MAXBUFF);
	iposix_utf8towc(path, out, maxsize);
	return path;
}


//---------------------------------------------------------------------
// platform special
//---------------------------------------------------------------------

// cross os GetModuleFileName, returns size for success, -1 for error
int iposix_path_executable(char *path, int maxsize)
{
	int retval = -1;
#if defined(_WIN32)
	DWORD len = GetModuleFileNameA(NULL, path, (DWORD)maxsize);
	if (len == 0 || (int)len == maxsize) {
        return -1;
    }
	path[len] = 0;
	retval = (int)len;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	int mib[4];
	size_t cb = (size_t)maxsize;
	int hr;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	hr = sysctl(mib, 4, path, &cb, NULL, 0);
	if (hr >= 0) retval = (int)cb;
#elif defined(linux) || defined(__CYGWIN__)
	ssize_t len = readlink("/proc/self/exe", path, maxsize - 1);
	if (len < 0 || len >= maxsize - 1) {
		return -1;
	}
	path[len] = 0;
	retval = (int)len;
#else
#endif
	if (retval >= 0 && retval < maxsize) {
		path[retval] = '\0';
	}	else if (maxsize > 0) {
		path[0] = '\0';
	}

	if (maxsize > 0) path[maxsize - 1] = 0;

	return retval;
}

// cross os GetModuleFileName, returns size for success, -1 for error
int iposix_path_wexecutable(wchar_t *path, int maxsize)
{
#if defined(_WIN32)
	DWORD len = GetModuleFileNameW(NULL, path, (DWORD)maxsize);
	if (len == 0 || (int)len == maxsize) {
        return -1;
    }
	path[len] = 0;
	return (int)len;
#else
	char buf[IPOSIX_MAXBUFF];
	int retval = iposix_path_executable(buf, IPOSIX_MAXBUFF);
	if (retval < 0) {
		if (maxsize > 0) path[0] = 0;
		return -1;
	}
	iposix_utf8towc(path, buf, maxsize);
	return (int)wcslen(path);
#endif
}

// retrive executable path directly 
const char *iposix_path_exepath(void)
{
	static int initialized = 0;
	static char exepath[IPOSIX_MAXPATH + 8] = {0};
	if (initialized == 0) {
		iposix_path_executable(exepath, IPOSIX_MAXPATH);
		initialized = 1;
	}
	if (exepath[0] == 0) return NULL;
	return exepath;
}

// retrive executable path directly 
const wchar_t *iposix_path_wexepath(void)
{
	static int initialized = 0;
	static wchar_t exepath[IPOSIX_MAXPATH + 8] = {0};
	if (initialized == 0) {
		iposix_path_wexecutable(exepath, IPOSIX_MAXPATH);
		initialized = 1;
	}
	if (exepath[0] == 0) return NULL;
	return exepath;
}

// 递归创建路径：直接从 ilog移植过来
int iposix_path_mkdir(const char *path, int mode)
{
	int i, len;
	char str[IPOSIX_MAXBUFF];

	len = (int)strlen(path);
	if (len > IPOSIX_MAXPATH) len = IPOSIX_MAXPATH;

	memcpy(str, path, len);
	str[len] = 0;

#ifdef _WIN32
	for (i = 0; i < len; i++) {
		if (str[i] == '/') str[i] = '\\';
	}
#endif

	for (i = 0; i < len; i++) {
		if (str[i] == '/' || str[i] == '\\') {
			str[i] = '\0';
			if (iposix_access(str, F_OK) != 0) {
				iposix_mkdir(str, mode);
			}
			str[i] = IPATHSEP;
		}
	}

	if (len > 0 && iposix_access(str, 0) != 0) {
		iposix_mkdir(str, mode);
	}

	return 0;
}


//=====================================================================
// System Utilities
//=====================================================================
#ifndef IDISABLE_SHARED_LIBRARY
	#if defined(__unix)
		#include <dlfcn.h>
	#endif
#endif

void *iposix_shared_open(const char *dllname)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	return dlopen(dllname, RTLD_LAZY);
	#else
	return (void*)LoadLibraryA(dllname);
	#endif
#else
	return NULL;
#endif
}

// LoadLibraryA
void *iposix_shared_wopen(const wchar_t *dllname)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	char buf[IPOSIX_MAXBUFF];
	iposix_wcstoutf8(buf, dllname, IPOSIX_MAXBUFF);
	return dlopen(buf, RTLD_LAZY);
	#else
	return (void*)LoadLibraryW(dllname);
	#endif
#else
	return NULL;
#endif
}

void *iposix_shared_get(void *shared, const char *name)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	return dlsym(shared, name);
	#else
	return (void*)GetProcAddress((HINSTANCE)shared, name);
	#endif
#else
	return NULL;
#endif
}

void iposix_shared_close(void *shared)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	dlclose(shared);
	#else
	FreeLibrary((HINSTANCE)shared);
	#endif
#endif
}


//---------------------------------------------------------------------
// load file content
//---------------------------------------------------------------------
void *iposix_path_load(const char *filename, long *size)
{
	long length, remain;
	char *ptr, *out;
	FILE *fp;

	if ((fp = fopen(filename, "rb")) == NULL) {
        if (size) size[0] = 0;
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

	// ftell error
	if (length < 0) {
		fclose(fp);
		if (size) size[0] = 0;
		return NULL;
	}
	
    // avoid zero-size file returns null
	ptr = (char*)malloc(length + 8);

	if (ptr == NULL) {
		fclose(fp);
		if (size) size[0] = 0;
		return NULL;
	}

	for (remain = length, out = ptr; remain > 0; ) {
		size_t ret = fread(out, 1, remain, fp);
		if (ret == 0) break;
        remain -= ret;
        out += ret;
	}

	fclose(fp);
	
	if (size) size[0] = (long)length;

	return ptr;
}


//---------------------------------------------------------------------
// wide-char: load file content, use free to dispose
//---------------------------------------------------------------------
void *iposix_path_wload(const wchar_t *filename, long *size)
{
	long length, remain;
	char *ptr, *out;
	FILE *fp;

#ifdef _WIN32
	fp = _wfopen(filename, L"rb");
#else
	char buf[IPOSIX_MAXBUFF];
	iposix_wcstoutf8(buf, filename, IPOSIX_MAXBUFF);
	fp = fopen(buf, "rb");
#endif

	if (fp == NULL) {
		if (size) size[0] = 0;
		return NULL;
	}

    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

	// ftell error
	if (length < 0) {
		fclose(fp);
		if (size) size[0] = 0;
		return NULL;
	}
	
    // avoid zero-size file returns null
	ptr = (char*)malloc(length + 8);

	if (ptr == NULL) {
		fclose(fp);
		if (size) size[0] = 0;
		return NULL;
	}

	for (remain = length, out = ptr; remain > 0; ) {
		size_t ret = fread(out, 1, remain, fp);
		if (ret == 0) break;
        remain -= ret;
        out += ret;
	}

	fclose(fp);
	
	if (size) size[0] = (long)length;

	return ptr;
}

// save file content
int iposix_path_save(const char *filename, const void *data, long size)
{
	const char *ptr = (const char*)data;
	FILE *fp;
	int hr = 0;
	if ((fp = fopen(filename, "wb")) == NULL) return -1;
	for (; size > 0; ) {
		long written = (long)fwrite(ptr, 1, size, fp);
		if (written <= 0) {
			hr = -2;
			break;
		}
		size -= written;
		ptr += written;
	}
	fclose(fp);
	return hr;
}

// wide-char: save file content
int iposix_path_wsave(const wchar_t *filename, const void *data, long size)
{
	const char *ptr = (const char*)data;
	FILE *fp;
	int hr = 0;
#ifdef _WIN32
	fp = _wfopen(filename, L"wb");
#else
	char buf[IPOSIX_MAXBUFF];
	iposix_wcstoutf8(buf, filename, IPOSIX_MAXBUFF);
	fp = fopen(buf, "wb");
#endif
	if (fp == NULL) return -1;

	for (; size > 0; ) {
		long written = (long)fwrite(ptr, 1, size, fp);
		if (written <= 0) {
			hr = -2;
			break;
		}
		size -= written;
		ptr += written;
	}
	fclose(fp);
	return hr;
}


// rename file: can replace the existing file atomically
int iposix_path_rename(const char *oldname, const char *newname)
{
#ifdef _WIN32
	if (MoveFileExA(oldname, newname, MOVEFILE_REPLACE_EXISTING) == 0) {
		return -1;
	}
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
	defined(__MACH__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
	defined(__OpenBSD__) || defined(__sun) || defined(__SVR4) || \
	defined(__CYGWIN__) || defined(__ANDROID__) || defined(__HAIKU__)
	if (rename(oldname, newname) != 0) {
		return -1;
	}
#else
	remove(newname);
	rename(oldname, newname);
#endif
	return 0;
}

// rename file: can replace the existing file atomically
int iposix_path_wrename(const wchar_t *oldname, const wchar_t *newname)
{
#ifdef _WIN32
	if (MoveFileExW(oldname, newname, MOVEFILE_REPLACE_EXISTING) == 0) {
		return -1;
	}
#else
	char buf1[IPOSIX_MAXBUFF];
	char buf2[IPOSIX_MAXBUFF];
	iposix_wcstoutf8(buf1, oldname, IPOSIX_MAXBUFF);
	iposix_wcstoutf8(buf2, newname, IPOSIX_MAXBUFF);
	return iposix_path_rename(buf1, buf2);
#endif
	return 0;
}



#endif




