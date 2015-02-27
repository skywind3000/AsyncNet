#! /usr/bin/env python
# -*- coding: utf-8 -*-
import sys, time, os


#----------------------------------------------------------------------
# clean_memo
#----------------------------------------------------------------------
def clean_memo (text):
	content = text
	spaces = (' ', '\n', '\t', '\r')
	import cStringIO
	srctext = cStringIO.StringIO()
	srctext.write(text)
	srctext.seek(0)
	memo = 0
	i = 0
	length = len(content)
	output = srctext.write
	while i < length:
		char = content[i]
		word = content[i : i + 2]
		if memo == 0:		# 正文中
			if word == '/*':
				output('``')
				i += 2
				memo = 1
				continue
			if word == '//':
				output('``')
				i += 2
				while (i < len(content)) and (content[i] != '\n'):
					if content[i] in spaces:
						output(content[i])
						i = i + 1
						continue						
					output('`')
					i = i + 1
				continue
			if char == '\"':
				output('\"')
				i += 1
				memo = 2
				continue
			if char == '\'':
				output('\'')
				i += 1
				memo = 3
				continue
			output(char)
		elif memo == 1:		# 注释中
			if word == '*/':
				output('``')
				i += 2
				memo = 0
				continue
			if char in spaces:
				output(content[i])
				i += 1
				continue
			output('`')
		elif memo == 2:		# 字符串中
			if word == '\\\"':
				output('\\\"')
				i += 2
				continue
			if word == '\\\\':
				output('\\\\')
				i += 2
				continue
			if char == '\"':
				output('\"')
				i += 1
				memo = 0
				continue
			if char in spaces:
				output(char)
				i += 1
				continue
			output(char)
		elif memo == 3:		# 字符中
			if word == '\\\'':
				output('\\\'')
				i += 2
				continue
			if word == '\\\\':
				output('\\\\')
				i += 2
				continue
			if char == '\'':
				output('\'')
				i += 1
				memo = 0
				continue
			if char in spaces:
				output(char)
				i += 1
				continue
			output(char)
		i += 1
	srctext.truncate()
	return srctext.getvalue().replace('`', ' ')


#----------------------------------------------------------------------
# interface
#----------------------------------------------------------------------
class Interface (object):
	def __init__ (self):
		self.name = ''
		self.res_type = None
		self.arg_types = []
		self.arg_names = []
		self.arg_count = 0
		self.must_return = False
	def __repr__ (self):
		text = 'Interface(%s):\n'%self.name
		text += '    return: %s\n'%self.res_type
		text += '    must_return: %s\n'%self.must_return
		text += '    arg_types: %s\n'%self.arg_types
		text += '    arg_names: %s\n'%self.arg_names
		return text
	def __str__ (self):
		return self.__repr__()
	def make_args (self):
		text = ''
		if self.arg_count > 0:
			suck = []
			for i in xrange(self.arg_count):
				t, n = self.arg_types[i], self.arg_names[i]
				x = '%s %s %s'%(t[0], n, t[1])
				suck.append(x.strip('\r\n\t '))
			text = ', '.join(suck)
		else:
			text = 'void'
		return '(%s)'%text
	def make_type (self):
		text = 'typedef %s (*_t_%s)'%(self.res_type, self.name)
		text += self.make_args()
		text += ';'
		return text
	def make_proc (self):
		text = '%s (*_p_%s)'%(self.res_type, self.name)
		text += self.make_args()
		text += ' = 0;'
		return text
	def make_call (self):
		text = '%s %s'%(self.res_type, self.name)
		text += self.make_args() + ' {\n\t'
		if self.must_return:
			text += 'return '
		text += '_p_%s('%self.name
		if self.arg_count > 0:
			text += ', '.join(self.arg_names)			
		text += ');\n'
		text += '}\n'
		return text


#----------------------------------------------------------------------
# interface parser
#----------------------------------------------------------------------
class IntParser (object):

	def __init__ (self, text):
		self.content = clean_memo(text).strip('\r\n\t ')
		self.interface = []
		self.intername = {}
		self.includes = []
		self.modname = ''
		self.package = ''
		self._scan_main()
	
	def _scan_main (self):
		intsrc = []
		for line in self.content.split('\n'):
			text = line.strip('\r\n\t ')
			if text[:1] == '%':
				if text[:7] == '%module':
					self.modname = text[8:].strip('\r\n\t ')
				elif text[:8] == '%include':
					self.includes.append(text[9:].strip('\r\n\t '))
				elif text[:8] == '%package':
					self.package = text[9:].strip('\r\n\t ')
			else:
				intsrc.append(line)
		t = ('\n'.join(intsrc)).split(';')
		intsrc = []
		for x in t:
			x = x.strip('\r\n\t ')
			if x:
				intsrc.append(x)
		for src in intsrc:
			int = self.parse(src)
			if int == None:
				print 'error parse function:'
				print src
				sys.exit()
			self.interface.append(int)
			self.intername[int.name] = int
		return len(self.interface)
	
	def parse (self, text):
		text = text.strip('\r\n\t ').replace('\t', ' ').replace('\n', ' ')
		content = text
		if not text:
			print 'syntax error: empty source'
			return None
		if not '(' in text:
			print 'syntax error: no interface defined'
			print text
			return None
		if not ')' in text:
			print 'syntax error: no interface defined'
			print text
			return None
		p1 = text.find('(')
		p2 = text.find(')')
		if p1 > p2:
			print 'syntax error: braket error'
			return None
		head = text[:p1].strip('\r\n\t ')
		body = text[p1 + 1: p2].strip('\r\n\t ')
		p1 = head.rfind(' ')
		if p1 < 0: 
			p1 = head.rfind('\t')
		int = Interface()
		int.name = head[p1 + 1:]
		int.must_return = True
		int.arg_count = 0
		if p1 >= 0:
			int.res_type = head[:p1].strip('\r\n\t ')
		if 'void' in [x.strip('\r\n\t ') for x in int.res_type.split(' ')]:
			if not '*' in int.res_type:
				int.must_return = False
		if not int.res_type:
			int.must_return  = False
		if not body:
			int.arg_types = []
			int.arg_names = []
		elif body == 'void':
			int.arg_types = []
			int.arg_names = []
		else:
			part = [ x.strip('\r\n\t ') for x in body.split(',') ]
			for text in part:
				arg_type, arg_name = self.extract(text)
				if arg_type == None or arg_name == None:
					print 'in function %s: unsupport parameter "%s"'%(int.name, text)
					print content
					sys.exit()
				int.arg_types.append(arg_type)
				int.arg_names.append(arg_name)
			int.arg_count = len(int.arg_types)
		return int

	def extract (self, arg):
		arg = arg.strip('\r\n\t ')
		arg_type = []
		arg_name = ''
		if not arg:
			return None, None
		import tokenize
		import StringIO
		g = tokenize.generate_tokens(StringIO.StringIO(arg).readline)
		tokens = []
		for t in g:
			if not t[0] in (tokenize.NL, tokenize.ENDMARKER):
				tokens.append(t)
		p = -1
		for i in xrange(len(tokens)):
			t = tokens[i]
			if t[0] == tokenize.NAME:
				p = i
		if p < 0:
			return None, None
		arg_name = tokens[p][1]
		g1 = ' '.join([ tokens[x][1] for x in xrange(p) ])
		g2 = ' '.join([ tokens[x][1] for x in xrange(p + 1, len(tokens)) ])
		g1 = g1.strip('\r\n\t ')
		g2 = g2.strip('\r\n\t ')
		if g2 == '[ ]': g2 = '[]'
		elif g2 == '[ ] [ ]': gs = '[][]'
		return (g1, g2), arg_name

	def __getitem__ (self, key):
		if type(key) in (type(''), type(u'')):
			return self.intername[key]
		return self.interface[key]
	
	def __contains__ (self, key):
		if type(key) in (type(''), type(u'')):
			return self.intername.__contains__(key)
		return self.interface.__contains__(key)
	
	def __len__ (self):
		return len(self.interface)
	
	def __iter__ (self):
		return self.interface.__iter__()


#----------------------------------------------------------------------
# GenLoader
#----------------------------------------------------------------------
class GenLoader (object):

	def __init__ (self, source):
		self.ints = IntParser(source)
	
	def generate (self):
		self.__build_c(self.ints.modname + '.c')
		return 0
	
	def __comment_head (self, fp, prefix):
		cline = prefix + '=' * (72 - len(prefix))
		fp.write(cline + '\n')
		fp.write(prefix + '\n')
		fp.write(prefix + ' %s.c - Dynamic Module Exporting\n'%self.ints.modname)
		fp.write(prefix + '\n')
		fp.write(prefix + ' This file is generated by GenLoader.py. Don\'t modify it.\n')
		fp.write(prefix + ' %s\n'%time.strftime('%Y-%m-%d %H:%M:%S', time.localtime()))
		fp.write(prefix + '\n')
		fp.write(cline + '\n')
	
	def __comment_block (self, fp, prefix, text, comment = '-'):
		cline = prefix + comment * (72 - len(prefix))
		fp.write('\n')
		fp.write(cline + '\n')
		fp.write(prefix + ' ' + text + '\n')
		fp.write(cline + '\n')
	
	def __make_initialize (self, fp):
		self.__comment_block(fp, '//', 'Initializer: load module, returns zero for success', '=')
		fp.write('int %s_Init(const char *dllname)\n{\n'%self.ints.modname)
		fp.write('\tvoid *mod = NULL;\n')
		fp.write('\tif (%s) return 0;\n'%self.var_inited)
		fp.write('#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)\n')
		fp.write('\tmod = (void*)LoadLibraryA(dllname);\n')
		fp.write('#else\n')
		fp.write('\tmod = (void*)dlopen(dllname, RTLD_LAZY);\n')
		fp.write('#endif\n')
		fp.write('\tif (mod == NULL) return -1;\n')
		fp.write('#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)\n')
		for int in self.ints:
			fp.write('\t_p_%s = (_t_%s)GetProcAddress((HINSTANCE)mod, "%s");\n'%(int.name, int.name, int.name))
		fp.write('#else\n')
		for int in self.ints:
			fp.write('\t_p_%s = (_t_%s)dlsym(mod, "%s");\n'%(int.name, int.name, int.name))
		fp.write('#endif\n')
		for i in xrange(len(self.ints)):
			fp.write('\tif (_p_%s == 0) return %d;\n'%(self.ints[i].name, i + 1))
		fp.write('\t%s = mod;\n'%self.var_module)
		fp.write('\t%s = 1;\n'%self.var_inited)
		fp.write('\treturn 0;\n')
		fp.write('}\n\n')
	
	def __make_quit (self, fp):
		self.__comment_block(fp, '//', 'Quit: Unload dynamic module', '=')
		fp.write('void %s_Quit(void)\n{\n'%self.ints.modname)
		fp.write('\tvoid *mod = %s;\n'%self.var_module)
		fp.write('\t%s = 0;\n\t%s = NULL;\n'%(self.var_inited, self.var_module))
		for int in self.ints:
			fp.write('\t_p_%s = 0;\n'%int.name)
		fp.write('\tif (mod != NULL) {\n')
		fp.write('#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)\n')
		fp.write('\t\tFreeLibrary((HINSTANCE)mod);\n')
		fp.write('#else\n')
		fp.write('\t\tdlclose(mod);\n')
		fp.write('#endif\n')
		fp.write('\t}\n')
		fp.write('}\n\n')

	def __build_c (self, name):
		fp = open(name, 'w')
		self.__comment_head(fp, '//')
		fp.write('#include <stdio.h>\n')
		fp.write('#include <stdlib.h>\n')
		fp.write('#include <string.h>\n')
		fp.write('#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)\n')
		fp.write('#include <windows.h>\n')
		fp.write('#else\n')
		fp.write('#include <unistd.h>\n')
		fp.write('#include <dlfcn.h>\n')
		fp.write('#endif\n')
		for inc in self.ints.includes:
			fp.write('#include ' + inc + '\n')
		fp.write('\n')
		self.__comment_block(fp, '//', 'Type Of Interfaces', '=')
		for int in self.ints:
			fp.write('%s\n'%int.make_type())
		fp.write('\n')
		self.__comment_block(fp, '//', 'Address Of Interfaces', '=')
		for int in self.ints:
			fp.write('%s\n'%int.make_proc())
		fp.write('\n')
		self.__comment_block(fp, '//', 'Internal Definition', '=')
		self.var_inited = '%s_inited'%self.ints.modname
		self.var_module = '%s_module'%self.ints.modname
		fp.write('static int %s = 0;\n'%self.var_inited)
		fp.write('static void* %s = NULL;\n'%self.var_module)
		fp.write('\n')
		self.__make_initialize(fp)
		self.__make_quit(fp)
		for int in self.ints:
			self.__comment_block(fp, '//', 'export: %s'%int.name, '-')
			fp.write(int.make_call())
		fp.write('\n\n')
		fp.close()
		print '%s generated'%name
		return 0



#----------------------------------------------------------------------
# main
#----------------------------------------------------------------------
def main():
	if len(sys.argv) == 1:
		print 'usage: python GenLoader.py interface.i'
		sys.exit()
	import os
	if not os.path.exists(sys.argv[1]):
		print 'can not open %s'%sys.argv[1]
		sys.exit()
	g = GenLoader(open(sys.argv[1]).read())
	g.generate()
	return 0


#----------------------------------------------------------------------
# testing case
#----------------------------------------------------------------------
if __name__ == '__main__':
	def test1():
		line = 0
		for x, y, _, _, _ in tokenizor(open('QuickLoader.h').read()):
			print x, y
			line += 1
			if line > 100: break
	def test2():
		p = IntParser(open('QuickLoader.i').read())
	def test3():
		p = IntParser(open('QuickLoader.i').read())
		t = 'QNETAPI void qnet_async_send_vector(AsyncCore *core, long hid, \n' + \
			'	const void *vecptr[], const long veclen[], int count, int mask);'
		print p.parse(t)
	def test4():
		g = GenLoader(open('QuickLoader.i').read())
		g.generate()
		return 0
	#sys.argv = [ '', 'CCLibLoader.i' ]
	#test4()
	main()


