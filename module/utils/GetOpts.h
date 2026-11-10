//=====================================================================
//
// GetOpts.h - 
//
// Last Modified: 2025/12/09 15:59:53
//
//=====================================================================
#ifndef _GET_OPTS_H_20251209155953_
#define _GET_OPTS_H_20251209155953_

#include <stddef.h>

#include <string>
#include <vector>
#include <map>



//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
namespace System {


//---------------------------------------------------------------------
// GetOpts class
//---------------------------------------------------------------------
class GetOpts final
{
public:

	// clear all options
	void Clear();

	// add new option
	void SetOption(const std::string &name, bool has_param);

	// parse args into _opts and _args according to optstring
	// throw std::invalid_argument when error occurs
	bool Load(const std::vector<std::string> &args);

	// parse args from argc/argv into _opts and _args according to optstring
	// throw std::invalid_argument when error occurs
	bool Load(int argc, char const *argv[], bool skip_first = true);

	// has option name
	bool HasOption(const std::string &name) const;

	// get option value by name, return default_value if not exist
	std::string GetOption(const std::string &name, const std::string &default_value = "") const;

	// get argument by index
	std::string GetArgument(int index) const;

	// get argument by index
	inline int size() const { return (int)_args.size(); }

	// get argument by index
	inline const std::string& operator [] (int index) const { return _args[index]; }

	// get argument by index
	inline std::string& operator [] (int index) { return _args[index]; }

	// To String
	std::string ToString() const;

private:

	// parse command line options from args according to optstring
	// throw std::invalid_argument when error occurs
	bool GetOptsFromArgs(
			const std::vector<std::string> &args,         // input arguments
			const std::string &optsting,                  // options which need params
			std::map<std::string, std::string> &opts,     // output options
			std::vector<std::string> &arguments);         // output remain arguments

private:
	std::map<std::string, bool> _setup_opts;
	std::map<std::string, std::string> _opts;
	std::vector<std::string> _args;
	std::vector<std::string> _origin_args;
};


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
}  // namespace System


#endif



