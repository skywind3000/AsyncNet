//=====================================================================
//
// GetOpts.cpp - 
//
// Last Modified: 2025/12/09 16:00:02
//
//=====================================================================
#include <string>
#include <sstream>

#include "GetOpts.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
namespace System {


//---------------------------------------------------------------------
// clear all options
//---------------------------------------------------------------------
void GetOpts::Clear()
{
	_setup_opts.clear();
	_opts.clear();
	_args.clear();
	_origin_args.clear();
}


//---------------------------------------------------------------------
// add new option
//---------------------------------------------------------------------
void GetOpts::SetOption(const std::string &name, bool has_param)
{
	_setup_opts[name] = has_param;
}


//---------------------------------------------------------------------
// parse args into _opts and _args according to optstring
// optstring: options which need parameters
// throw std::invalid_argument when error occurs
//---------------------------------------------------------------------
bool GetOpts::Load(const std::vector<std::string> &args)
{
	std::string optstring;
	for (const auto &_opt_ : _setup_opts) {
		std::string name = _opt_.first;
		if (name.size() == 1 && _opt_.second) {
			optstring.append(1, _opt_.first[0]);
		}
	}
	bool hr = GetOptsFromArgs(args, optstring, _opts, _args);
	_origin_args = args;
	if (hr == false) return false;
	for (auto &_opt : _opts) {
		auto it = _setup_opts.find(_opt.first);
		if (it == _setup_opts.end()) {
			std::string t = "unknown option: ";
			t.append(_opt.first);
			throw std::invalid_argument(t);
		}
	}
	return true;
}


//---------------------------------------------------------------------
// parse args from argc/argv into _opts and _args according to optstring
// optstring: options which need parameters
// throw std::invalid_argument when error occurs
//---------------------------------------------------------------------
bool GetOpts::Load(int argc, char const *argv[], bool skip_first)
{
	std::vector<std::string> args;
	for (int i = skip_first ? 1 : 0; i < argc; i++) {
		args.push_back(std::string(argv[i]));
	}
	bool hr = Load(args);
	return hr;
}


//---------------------------------------------------------------------
// has option name
//---------------------------------------------------------------------
bool GetOpts::HasOption(const std::string &name) const
{
	auto it = _opts.find(name);
	return (it != _opts.end());
}


//---------------------------------------------------------------------
// get option value by name, return default_value if not exist
//---------------------------------------------------------------------
std::string GetOpts::GetOption(const std::string &name, const std::string &default_value) const
{
	auto it = _opts.find(name);
	if (it != _opts.end()) {
		return it->second;
	}
	return default_value;
}


//---------------------------------------------------------------------
// get argument by index
//---------------------------------------------------------------------
std::string GetOpts::GetArgument(int index) const
{
	if (index >= 0 && index < (int)_args.size()) {
		return _args[index];
	}
	return "";
}


//---------------------------------------------------------------------
// To String
//---------------------------------------------------------------------
std::string GetOpts::ToString() const
{
	std::stringstream ss;
	ss << "GetOpts({";
	bool first = true;
	for (const auto &opt : _opts) {
		if (!first) {
			ss << ", ";
		}
		if (opt.second.size() > 0) {
			ss << "\"" << opt.first << "\": \"" << opt.second << "\"";
		} else {
			ss << "\"" << opt.first << "\"";
		}
		first = false;
	}
	ss << "}, [";
	first = true;
	for (const auto &arg : _args) {
		if (!first) {
			ss << ", ";
		}
		ss << "\"" << arg << "\"";
		first = false;
	}
	ss << "])";
	return ss.str();
}


//---------------------------------------------------------------------
// parse command line options from args according to optstring
// throw std::invalid_argument when error occurs
//---------------------------------------------------------------------
bool GetOpts::GetOptsFromArgs(
		const std::vector<std::string> &args,         // input arguments
		const std::string &optsting,                  // options which need params
		std::map<std::string, std::string> &opts,     // output options
		std::vector<std::string> &arguments)          // output remain arguments
{
	int count = (int)args.size(), index = 0;
	opts.clear();
	arguments.clear();
	if (count == 0) {
		return true;
	}
	for (index = 0; index < count; ) {
		const std::string &arg = args[index];
		if (arg.size() >= 2) {
			if (arg[0] == '-' && arg[1] == '-') {
				std::string name = arg.substr(2);
				if (name.empty()) {
					// "--" indicates end of options
					index++;
					break;
				}
				auto pos = name.find('=');
				if (pos != std::string::npos) {
					std::string optname = name.substr(0, pos);
					std::string optvalue = name.substr(pos + 1);
					opts[optname] = optvalue;
					index++;
				}
				else {
					opts[name] = "";
					index++;
				}
			}
			else if (arg[0] == '-') {
				char ch = arg[1];
				auto pos = optsting.find(ch);
				if (pos != std::string::npos) {
					if (index < count - 1) {
						std::string optvalue = args[index + 1];
						opts[std::string(1, ch)] = optvalue;
						index += 2;
					}
					else {
						// missing parameter
						std::string t = "missing parameter for option -";
						t.append(1, ch);
						throw std::invalid_argument(t);
					}
				} else {
					opts[std::string(1, ch)] = "";
					index++;
				}
			}
			else {
				break;
			}
		}
		else {
			break;
		}
	}
	for (; index < count; index++) {
		arguments.push_back(args[index]);
	}
	return true;
}


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
}  // namespace System



