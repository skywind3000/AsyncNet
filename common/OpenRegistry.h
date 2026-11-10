//=====================================================================
//
// OpenRegistry.h - Sectioned string registry utilities
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
#ifndef _OPEN_REGISTRY_H_
#define _OPEN_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>


namespace System {

//---------------------------------------------------------------------
// OpenRegistry: a simple section-grouped key-value store
//---------------------------------------------------------------------
class OpenRegistry final
{
public:
	virtual ~OpenRegistry();
	OpenRegistry();
	OpenRegistry(const OpenRegistry &other);
	OpenRegistry(OpenRegistry &&other) noexcept;

	OpenRegistry& operator = (const OpenRegistry &other);
	OpenRegistry& operator = (OpenRegistry &&other) noexcept;

public:

	// remove all sections and keys
	void Clear();

	// get section count
	inline int Count() const { return (int)_sections.size(); }
	
	// check section existence
	bool HasSection(const std::string &section) const;

	// Get section by index
	std::string GetSection(int index) const;

	// create a section if not exists
	void AddSection(const std::string &section);

	// remove a section
	void RemoveSection(const std::string &section);
	
	// clear a section
	void ClearSection(const std::string &section);

	// check key existence
	bool HasValue(const std::string &section, const std::string &key) const;

	// remove a value from section
	void RemoveValue(const std::string &section, const std::string &key);

	// get all keys in a section
	std::vector<std::string> GetKeys(const std::string &section) const;

	// Read value from registry
	std::string ReadValue(const std::string &section, const std::string &key, const std::string &default_value = "") const;

	// Write value to registry
	void WriteValue(const std::string &section, const std::string &key, const std::string &value);

	// Read integer value from registry
	int ReadInt(const std::string &section, const std::string &key, int default_value = 0) const;

	// Read 64-bits integer value from registry
	int64_t ReadInt64(const std::string &section, const std::string &key, int64_t default_value = 0) const;

	// Read float
	double ReadFloat(const std::string &section, const std::string &key, double default_value = 0.0) const;

	// Read bool
	bool ReadBool(const std::string &section, const std::string &key, bool default_value = false) const;

	// Write integer value to registry
	void WriteInt(const std::string &section, const std::string &key, int value);

	// Write 64-bits integer value to registry
	void WriteInt64(const std::string &section, const std::string &key, int64_t value);

	// Write float
	void WriteFloat(const std::string &section, const std::string &key, double value);

	// write bool
	void WriteBool(const std::string &section, const std::string &key, bool value);

public:

	// load from file
	bool LoadFile(const std::string &filename);

	// save to file
	bool SaveFile(const std::string &filename) const;

	// marshal to stream
	bool Marshal(std::ostream &ofs) const;

	// unmarshal from stream
	bool Unmarshal(std::istream &ifs);

	// atomic save to file, prevent data corruption
	bool AtomicSaveFile(const std::string &filename) const;

	// load ini file
	bool LoadIniFile(const std::string &ininame);

	// save ini file
	bool SaveIniFile(const std::string &ininame) const;

	// dump text to ostream
	void DumpToStream(std::ostream &os) const;

protected:

	// strip a string
	static void StringStrip(std::string &str, const char *seps = NULL);

	// remove \r\n from string
	static std::string StringClear(const std::string &str, const char *remove = "\r\n");

	// string lower
	static void StringLower(std::string &str);

	// string upper
	static void StringUpper(std::string &str);

	// generate unique ID string
	static std::string UniqueID();

	// replace file
	static bool FileReplace(const std::string &newname, const std::string &oldname);

	// check file existence
	static bool FileExists(const std::string &filename);

	// stream read uint32
	static uint32_t StreamReadUInt32(std::istream &ifs);

	// stream write uint32
	static void StreamWriteUInt32(std::ostream &ofs, uint32_t value);

	// stream read string
	static std::string StreamReadString(std::istream &ifs);

	// stream write string
	static void StreamWriteString(std::ostream &ofs, const std::string &str);

private:

	using RegistrySection = std::unordered_map<std::string, std::string>;
	using RegistryBook = std::unordered_map<std::string, RegistrySection>;

	RegistryBook  _book;
	std::vector<std::string> _sections;
	std::unordered_map<std::string, int> _positions;
};



//---------------------------------------------------------------------
// Help Functions
//---------------------------------------------------------------------

// operator
inline std::ostream& operator << (std::ostream &os, const OpenRegistry &reg) {
	reg.DumpToStream(os);
	return os;
}



}  // namespace System


#endif


