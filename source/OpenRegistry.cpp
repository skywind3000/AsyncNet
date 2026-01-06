//=====================================================================
//
// OpenRegistry.cpp - Sectioned string registry utilities
//
// Created by skywind on 2021/11/02
// Last Modified: 2025/12/11 16:15:35
//
// OpenRegistry manages string-to-string settings grouped by sections.
// It tracks section order, offers typed helpers, and persists data 
// via both QREG binary snapshots and INI-style text files:
//
// - Add, remove, query, and enumerate sections/keys while preserving
//   stable indices for deterministic serialization.
// - Read/write plain strings plus typed int/double helpers.
// - Load/save compact binary blobs or human-readable INI documents
//   and dump formatted output to arbitrary streams.
// - Provide trimming/cleanup helpers to normalize textual input.
//
// Usage example:
//
//  System::OpenRegistry reg;
//  reg.LoadFile("config.dat");
//  int width = reg.ReadInt("Window", "Width", 800);
//  int height = reg.ReadInt("Window", "Height", 600);
//  reg.WriteInt("Window", "Width", width);
//  reg.WriteInt("Window", "Height", height);
//  reg.SaveFile("config.dat");
//
// For more details, see OpenRegistry.cpp.
//
//=====================================================================
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <string>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
#ifndef WIN32_LEAN_AND_MEAN  
#define WIN32_LEAN_AND_MEAN  
#endif
#define IHAVE_MOVE_FILE_EX  1
#include <windows.h>
#include <winbase.h>
#endif

#include "OpenRegistry.h"

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

namespace System {

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
OpenRegistry::~OpenRegistry()
{

}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
OpenRegistry::OpenRegistry()
{

}


//---------------------------------------------------------------------
// copy ctor
//---------------------------------------------------------------------
OpenRegistry::OpenRegistry(const OpenRegistry &other): 
	_book(other._book), _sections(other._sections),
	_positions(other._positions)
{

}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
OpenRegistry::OpenRegistry(OpenRegistry &&other) noexcept:
	_book(std::move(other._book)), _sections(std::move(other._sections)),
	_positions(std::move(other._positions))
{

}


//---------------------------------------------------------------------
// copy assignment
//---------------------------------------------------------------------
OpenRegistry& OpenRegistry::operator = (const OpenRegistry &other)
{
	if (this != &other) {
		_book = other._book;
		_sections = other._sections;
		_positions = other._positions;
	}
	return *this;
}


//---------------------------------------------------------------------
// move assignment
//---------------------------------------------------------------------
OpenRegistry& OpenRegistry::operator = (OpenRegistry &&other) noexcept
{
	if (this != &other) {
		_book = std::move(other._book);
		_sections = std::move(other._sections);
		_positions = std::move(other._positions);
	}
	return *this;
}


//---------------------------------------------------------------------
// remove all sections and keys
//---------------------------------------------------------------------
void OpenRegistry::Clear()
{
	_book.clear();
	_sections.clear();
	_positions.clear();
}


//---------------------------------------------------------------------
// check section existence
//---------------------------------------------------------------------
bool OpenRegistry::HasSection(const std::string &section) const
{
	return (_book.find(section) != _book.end());
}


//---------------------------------------------------------------------
// Get section by index
//---------------------------------------------------------------------
std::string OpenRegistry::GetSection(int index) const
{
	if (index < 0 || index >= (int)_sections.size()) {
		return "";
	}
	return _sections[index];
}


//---------------------------------------------------------------------
// create a section if not exists
//---------------------------------------------------------------------
void OpenRegistry::AddSection(const std::string &section)
{
	auto sec_it = _book.find(section);
	if (sec_it == _book.end()) {
		_book[section] = RegistrySection();
		_sections.push_back(section);
		_positions[section] = (int)_sections.size() - 1;
	}
}


//---------------------------------------------------------------------
// remove a section
//---------------------------------------------------------------------
void OpenRegistry::RemoveSection(const std::string &section)
{
	auto sec_it = _book.find(section);
	if (sec_it != _book.end()) {
		auto pos_it = _positions.find(section);
		if (pos_it == _positions.end()) {
			assert(pos_it != _positions.end());
			return;
		}
		int position = pos_it->second;
		_book.erase(sec_it);
		if (_positions.size() <= 1) {
			_positions.clear();
			_sections.clear();
		}
		else {
			int last_index = (int)_sections.size() - 1;
			if (position != last_index) {
				std::string last_section = _sections[last_index];
				_sections[position] = _sections[last_index];
				_positions[last_section] = position;
			}
			_sections.pop_back();
			_positions.erase(section);
		}
	}
}


//---------------------------------------------------------------------
// clear a section
//---------------------------------------------------------------------
void OpenRegistry::ClearSection(const std::string &section)
{
	auto sec_it = _book.find(section);
	if (sec_it != _book.end()) {
		sec_it->second.clear();
	}
}


//---------------------------------------------------------------------
// check key existence
//---------------------------------------------------------------------
bool OpenRegistry::HasValue(const std::string &section, const std::string &key) const
{
	auto sec_it = _book.find(section);
	if (sec_it != _book.end()) {
		const RegistrySection &reg_section = sec_it->second;
		return (reg_section.find(key) != reg_section.end());
	}
	return false;
}


//---------------------------------------------------------------------
// remove a value from section
//---------------------------------------------------------------------
void OpenRegistry::RemoveValue(const std::string &section, const std::string &key)
{
	auto sec_it = _book.find(section);
	if (sec_it != _book.end()) {
		RegistrySection &reg_section = sec_it->second;
		auto key_it = reg_section.find(key);
		if (key_it != reg_section.end()) {
			reg_section.erase(key_it);
		}
	}
}


//---------------------------------------------------------------------
// get all keys in a section
//---------------------------------------------------------------------
std::vector<std::string> OpenRegistry::GetKeys(const std::string &section) const
{
	std::vector<std::string> keys;
	auto sec_it = _book.find(section);
	if (sec_it != _book.end()) {
		const RegistrySection &reg_section = sec_it->second;
		for (const auto &kv : reg_section) {
			keys.push_back(kv.first);
		}
	}
	return keys;
}


//---------------------------------------------------------------------
// Read registry from ByteArray
//---------------------------------------------------------------------
std::string OpenRegistry::ReadValue(const std::string &section, const std::string &key, const std::string &default_value) const
{
	auto sec_it = _book.find(section);
	if (sec_it != _book.end()) {
		const RegistrySection &reg_section = sec_it->second;
		auto key_it = reg_section.find(key);
		if (key_it != reg_section.end()) {
			return key_it->second;
		}
	}
	return default_value;
}


//---------------------------------------------------------------------
// Write value to registry
//---------------------------------------------------------------------
void OpenRegistry::WriteValue(const std::string &section, const std::string &key, const std::string &value)
{
	auto sec_it = _book.find(section);
	if (sec_it == _book.end()) {
		AddSection(section);
		sec_it = _book.find(section);
	}
	if (sec_it != _book.end()) {
		RegistrySection &reg_section = sec_it->second;
		reg_section[key] = value;
	}
}


//---------------------------------------------------------------------
// Read integer value from registry
//---------------------------------------------------------------------
int OpenRegistry::ReadInt(const std::string &section, const std::string &key, int default_value) const
{
	std::string text = ReadValue(section, key, "");
	if (text.empty()) {
		return default_value;
	}
	try {
		return std::stoi(text);
	}
	catch (...) {
	}
	return default_value;
}


//---------------------------------------------------------------------
// Write integer value to registry
//---------------------------------------------------------------------
void OpenRegistry::WriteInt(const std::string &section, const std::string &key, int value)
{
	WriteValue(section, key, std::to_string(value));
}


//---------------------------------------------------------------------
// Read 64-bits integer value from registry
//---------------------------------------------------------------------
int64_t OpenRegistry::ReadInt64(const std::string &section, const std::string &key, int64_t default_value) const
{
	std::string text = ReadValue(section, key, "");
	if (text.empty()) {
		return default_value;
	}
	try {
		return (int64_t)std::stoll(text);
	}
	catch (...) {
	}
	return default_value;
}


//---------------------------------------------------------------------
// Write 64-bits integer value to registry
//---------------------------------------------------------------------
void OpenRegistry::WriteInt64(const std::string &section, const std::string &key, int64_t value)
{
	WriteValue(section, key, std::to_string(value));
}


//---------------------------------------------------------------------
// Read float
//---------------------------------------------------------------------
double OpenRegistry::ReadFloat(const std::string &section, const std::string &key, double default_value) const
{
	std::string text = ReadValue(section, key, "");
	if (text.empty()) {
		return default_value;
	}
	try {
		return std::stod(text); 
	}
	catch (...) {
	}
	return default_value;
}


//---------------------------------------------------------------------
// Write float
//---------------------------------------------------------------------
void OpenRegistry::WriteFloat(const std::string &section, const std::string &key, double value)
{
	std::stringstream ss;
	ss << std::setprecision(17) << value;
	WriteValue(section, key, ss.str());
}


//---------------------------------------------------------------------
// Read bool
//---------------------------------------------------------------------
bool OpenRegistry::ReadBool(const std::string &section, const std::string &key, bool default_value) const
{
	std::string text = ReadValue(section, key, "");
	if (text.empty()) {
		return default_value;
	}
	StringLower(text);
	if (text == "1" || text == "yes" || text == "true" || text == "on") {
		return true;
	}
	else if (text == "t" || text == "y") {
		return true;
	}
	return false;
}


//---------------------------------------------------------------------
// write bool
//---------------------------------------------------------------------
void OpenRegistry::WriteBool(const std::string &section, const std::string &key, bool value)
{
	if (value) {
		WriteValue(section, key, "true");
	}
	else {
		WriteValue(section, key, "false");
	}
}


//---------------------------------------------------------------------
// string lower
//---------------------------------------------------------------------
void OpenRegistry::StringLower(std::string &str)
{
	for (int i = 0; i < (int)str.size(); i++) {
		char &ch = str[i];
		if (ch >= 'A' && ch <= 'Z') {
			ch = (char)(ch - 'A' + 'a');
		}
	}
}


//---------------------------------------------------------------------
// string upper
//---------------------------------------------------------------------
void OpenRegistry::StringUpper(std::string &str)
{
	for (int i = 0; i < (int)str.size(); i++) {
		char &ch = str[i];
		if (ch >= 'a' && ch <= 'z') {
			ch = (char)(ch - 'a' + 'A');
		}
	}
}


//---------------------------------------------------------------------
// marshal to stream
//---------------------------------------------------------------------
bool OpenRegistry::Marshal(std::ostream &ofs) const
{
	ofs.write("OREG", 4);
	int32_t section_count = (int32_t)_sections.size();
	StreamWriteUInt32(ofs, (uint32_t)section_count);
	std::vector<std::string> sorted_sections = _sections;
	std::sort(sorted_sections.begin(), sorted_sections.end());
	for (auto &section_name : sorted_sections) {
		ofs.write("SECT", 4);
		StreamWriteString(ofs, section_name);
		auto sec_it = _book.find(section_name);
		if (sec_it != _book.end()) {
			const RegistrySection &reg_section = sec_it->second;
			int32_t key_count = (int32_t)reg_section.size();
			ofs.write("KEYS", 4);
			StreamWriteUInt32(ofs, (uint32_t)key_count);
			std::vector<std::string> sorted_keys;
			for (const auto &kv : reg_section) {
				sorted_keys.push_back(kv.first);
			}
			std::sort(sorted_keys.begin(), sorted_keys.end());
			for (const auto &key : sorted_keys) {
				const std::string &value = reg_section.at(key);
				StreamWriteString(ofs, key);
				StreamWriteString(ofs, value);
			}
		}
		ofs.write("SEND", 4);
	}
	ofs.write("END", 3);
	return true;
}


//---------------------------------------------------------------------
// unmarshal from stream
//---------------------------------------------------------------------
bool OpenRegistry::Unmarshal(std::istream &ifs)
{
	char header[4] = {0, 0, 0, 0};
	ifs.read(header, 4);
	if (strncmp(header, "OREG", 4) != 0) {
		return false;
	}
	Clear();
	int32_t section_count = (int32_t)StreamReadUInt32(ifs);
	for (int32_t i = 0; i < section_count; i++) {
		ifs.read(header, 4);
		if (strncmp(header, "SECT", 4) != 0) {
			return false;
		}
		std::string section_name = StreamReadString(ifs);
		AddSection(section_name);
		ifs.read(header, 4);
		if (strncmp(header, "KEYS", 4) != 0) {
			return false;
		}
		int32_t key_count = (int32_t)StreamReadUInt32(ifs);
		for (int32_t j = 0; j < key_count; j++) {
			std::string key = StreamReadString(ifs);
			std::string value = StreamReadString(ifs);
			WriteValue(section_name, key, value);
		}
		ifs.read(header, 4);
		if (strncmp(header, "SEND", 4) != 0) {
			return false;
		}
	}
	ifs.read(header, 3);
	if (strncmp(header, "END", 3) != 0) {
		return false;
	}
	return true;
}


//---------------------------------------------------------------------
// save to file
//---------------------------------------------------------------------
bool OpenRegistry::SaveFile(const std::string &filename) const
{
	std::ofstream ofs(filename, std::ios::binary);
	if (!ofs.is_open()) {
		return false;
	}
	bool hr = Marshal(ofs);
	ofs.close();
	return hr;
}


//---------------------------------------------------------------------
// load from file
//---------------------------------------------------------------------
bool OpenRegistry::LoadFile(const std::string &filename)
{
	std::ifstream ifs(filename,  std::ios::binary);
	if (!ifs.is_open()) {
		return false;
	}
	bool hr = Unmarshal(ifs);
	ifs.close();
	return hr;
}


//---------------------------------------------------------------------
// atomic save to file, prevent data corruption
//---------------------------------------------------------------------
bool OpenRegistry::AtomicSaveFile(const std::string &filename) const
{
	std::string tempname = filename + "." + UniqueID() + ".tmp";
	if (!SaveFile(tempname)) {
		return false;
	}
	if (!FileExists(tempname)) {
		return false;
	}
	bool hr = false;
	hr = FileReplace(filename, tempname);
#if 0
	printf("AtomicSaveFile: %s -> %s : %s\n",
		tempname.c_str(),
		filename.c_str(),
		(hr ? "success" : "failed"));
#endif
	if (FileExists(tempname)) {
		try {
			std::remove(tempname.c_str());
		}
		catch (...) {
		}
	}
	return hr;
}


//---------------------------------------------------------------------
// save ini file
//---------------------------------------------------------------------
bool OpenRegistry::SaveIniFile(const std::string &ininame) const
{
	std::ofstream ofs(ininame);
	if (!ofs.is_open()) {
		return false;
	}
	DumpToStream(ofs);
	return true;
}


//---------------------------------------------------------------------
// load ini file
//---------------------------------------------------------------------
bool OpenRegistry::LoadIniFile(const std::string &ininame)
{
	std::ifstream ifs(ininame);
	if (!ifs.is_open()) {
		return false;
	}
	Clear();	
	std::string text = "";
	std::string section_name = "default";
	bool firstline = true;
	while (std::getline(ifs, text)) {
		if (firstline) {
			firstline = false;
			if (text.size() >= 3) {
				if (text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF') {
					text = text.substr(3);
				}
			}
		}
		StringStrip(text, "\r\n\t ");
		if (text.empty()) continue;
		if (text[0] == ';' || text[0] == '#') continue;
		if (text[0] == '[') {
			size_t pos = text.find(']');
			if (pos == std::string::npos) continue;
			std::string name = text.substr(1, pos - 1);
			StringStrip(name);
			if (name.empty()) continue;
			section_name = name;
			AddSection(section_name);
		}
		else {
			size_t pos = text.find('=');
			if (pos == std::string::npos) continue;
			std::string key = text.substr(0, pos);
			std::string value = text.substr(pos + 1);
			StringStrip(key);
			StringStrip(value);
			if (!key.empty()) {
				WriteValue(section_name, key, value);
			}
		}
	}
	return true;
}


//---------------------------------------------------------------------
// dump text to ostream
//---------------------------------------------------------------------
void OpenRegistry::DumpToStream(std::ostream &os) const
{
	std::vector<std::string> sorted_sections = _sections;
	std::sort(sorted_sections.begin(), sorted_sections.end());
	for (auto &section_name : sorted_sections) {
		std::string striped = StringClear(section_name, "\r\n\t[] ");
		os << "[" << striped << "]" << std::endl;
		std::vector<std::string> keys = GetKeys(section_name);
		std::sort(keys.begin(), keys.end());
		for (auto &key : keys) {
			std::string value = ReadValue(section_name, key, "");
			std::string striped_key = StringClear(key, "\r\n\t=");
			std::string striped_value = StringClear(value, "\r\n");
			os << striped_key << "=" << striped_value << std::endl;
		}
		os << std::endl;
	}
}


//---------------------------------------------------------------------
// string a string
//---------------------------------------------------------------------
void OpenRegistry::StringStrip(std::string &str, const char *seps)
{
	size_t p1, p2, i;
	if (str.size() == 0) return;
	if (seps == NULL) seps = "\r\n\t ";
	for (p1 = 0; p1 < str.size(); p1++) {
		char ch = str[p1];
		int skip = 0;
		for (i = 0; seps[i]; i++) {
			if (ch == seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	if (p1 >= str.size()) {
		str.assign("");
		return;
	}
	for (p2 = str.size() - 1; p2 >= p1; p2--) {
		char ch = str[p2];
		int skip = 0;
		for (i = 0; seps[i]; i++) {
			if (ch == seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	str = str.substr(p1, p2 - p1 + 1);
}


//---------------------------------------------------------------------
// remove \r\n from string
//---------------------------------------------------------------------
std::string OpenRegistry::StringClear(const std::string &str, const char *remove)
{
	std::string out = str;
	for (int i = 0; i < (int)out.size(); i++) {
		char ch = out[i];
		int skip = 0;
		for (int j = 0; remove[j]; j++) {
			if (ch == remove[j]) {
				skip = 1;
				break;
			}
		}
		if (skip) {
			out[i] = ' ';
		}
	}
	return out;
}


//---------------------------------------------------------------------
// Generate Unique ID
//---------------------------------------------------------------------
std::string OpenRegistry::UniqueID()
{
	auto now = std::chrono::system_clock::now();
	auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
	std::random_device rd;
	char rnd[20];
	sprintf(rnd, "%03d", rd() % 1000);
	return std::to_string(ts.count()) + std::string(rnd);
}


//---------------------------------------------------------------------
// replace file
//---------------------------------------------------------------------
bool OpenRegistry::FileReplace(const std::string &newname, const std::string &oldname)
{
// #undef IHAVE_MOVE_FILE_EX
#if IHAVE_MOVE_FILE_EX
	BOOL hr = ::MoveFileExA(
		oldname.c_str(),
		newname.c_str(),
		MOVEFILE_REPLACE_EXISTING);
	if (hr == FALSE) {
		return false;
	}
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
	defined(__MACH__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
	defined(__OpenBSD__) || defined(__sun) || defined(__SVR4) || \
	defined(__CYGWIN__) || defined(__ANDROID__) || defined(__HAIKU__)
	try {
		if (std::rename(oldname.c_str(), newname.c_str()) != 0) {
			return false;
		}
	}
	catch (...) {
		return false;
	}
#else
	try {
		try {
			std::remove(newname.c_str());
		}
		catch (...) {
		}
		if (std::rename(oldname.c_str(), newname.c_str()) != 0) {
			return false;
		}
	}
	catch (...) {
		return false;
	}
#endif
	return true;
}


//---------------------------------------------------------------------
// check file existence
//---------------------------------------------------------------------
bool OpenRegistry::FileExists(const std::string &filename)
{
	std::ifstream ifs(filename);
	if (ifs.is_open()) {
		ifs.close();
		return true;
	}
	return false;
}


//---------------------------------------------------------------------
// stream read uint32
//---------------------------------------------------------------------
uint32_t OpenRegistry::StreamReadUInt32(std::istream &ifs)
{
	unsigned char head[4];
	ifs.read((char*)head, 4);
	if (ifs.gcount() != 4) {
		return 0;
	}
	uint32_t value = 0;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
	memcpy(&value, head, 4);
#else
	value |= ((uint32_t)head[0]) << 0;
	value |= ((uint32_t)head[1]) << 8;
	value |= ((uint32_t)head[2]) << 16;
	value |= ((uint32_t)head[3]) << 24;
#endif
	return value;
}


//---------------------------------------------------------------------
// stream write uint32
//---------------------------------------------------------------------
void OpenRegistry::StreamWriteUInt32(std::ostream &ofs, uint32_t value)
{
	unsigned char head[4];
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
	memcpy(head, &value, 4);
#else
	head[0] = (unsigned char)((value >> 0) & 0xFF);
	head[1] = (unsigned char)((value >> 8) & 0xFF);
	head[2] = (unsigned char)((value >> 16) & 0xFF);
	head[3] = (unsigned char)((value >> 24) & 0xFF);
#endif
	ofs.write((const char*)head, 4);
}


//---------------------------------------------------------------------
// stream read string
//---------------------------------------------------------------------
std::string OpenRegistry::StreamReadString(std::istream &ifs)
{
	uint32_t size = StreamReadUInt32(ifs);
	if (size == 0 || size >= 0x80000000) {
		return "";
	}
	std::string str;
	str.resize(size);
	ifs.read(&str[0], size);
	if ((uint32_t)ifs.gcount() != size) {
		return "";
	}
	return str;
}


//---------------------------------------------------------------------
// stream write string
//---------------------------------------------------------------------
void OpenRegistry::StreamWriteString(std::ostream &ofs, const std::string &str)
{
	size_t size = str.size();
	if (size >= 0x80000000) {
		StreamWriteUInt32(ofs, 0);
		return;
	}
	StreamWriteUInt32(ofs, (uint32_t)size);
	ofs.write(str.c_str(), size);
}



}  // namespace System


