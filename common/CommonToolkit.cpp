//=====================================================================
//
// CommonToolkit.cpp - 
//
// Last Modified: 2023/05/22 10:11:46
//
//=====================================================================
#include <mutex>

#include "CommonToolkit.h"
#include "GetHostAddress.h"



//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// parse comma separated binding addresses, '*' for all addresses
//---------------------------------------------------------------------
bool ParseBindingAddresses(const std::string &bindingString, StringList &addresses)
{
	std::string text = bindingString;
	StringStrip(text);
	addresses.clear();
	if (text.empty()) {
		return false;
	}
	else if (text == "*") {
		return GetHostAddressList(AF_INET, addresses);
	}
	else if (text == "**") {
		return GetHostAddressList(AF_INET6, addresses);
	}
	else if (text == "***") {
		if (!GetHostAddressList(AF_INET, addresses)) {
			return false;
		}
		StringList ipv6_addresses;
		if (!GetHostAddressList(AF_INET6, ipv6_addresses)) {
			ipv6_addresses.clear();
		}
		for (auto & addr : ipv6_addresses) {
			addresses.push_back(addr);
		}
		return true;
	}
	StringList parts;
	StringSplit(text, parts, ",");
	for (auto & part : parts) {
		StringStrip(part);
		if (part.empty()) {
			continue;
		}
		addresses.push_back(part);
	}
	return true;
}


//---------------------------------------------------------------------
// check if address is valid local address
//---------------------------------------------------------------------
bool IsLocalAddress(const std::string &address)
{
	std::string addr = address;
	StringList addresses;
	std::vector<PosixAddress> addrlist;
	int family = AF_INET;
	if (addr.find(':') != std::string::npos) {
		family = AF_INET6;
		StringStrip(addr, "[]");
	}
	if (!GetHostAddressList(family, addresses)) {
		return false;
	}
	for (auto & localaddr : addresses) {
		PosixAddress pa;
		pa.Zero();
		pa.SetFamily(family);
		pa.SetPort(0);
		pa.SetIpText(localaddr);
		addrlist.push_back(pa);
	}
	PosixAddress checkaddr;
	checkaddr.Zero();
	checkaddr.SetFamily(family);
	checkaddr.SetIpText(addr);
	for (auto & localaddr : addrlist) {
		if (checkaddr.IPEquals(localaddr)) {
			return true;
		}
	}
	return false;
}


//---------------------------------------------------------------------
// parse command line options from args according to optstring
//---------------------------------------------------------------------
bool GetOptsFromArgs(
		const std::vector<std::string> &args,         // input arguments
		const std::string &optstring,                 // options which need params
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
					// invalid option
					return false;
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
				auto pos = optstring.find(ch);
				if (pos != std::string::npos) {
					if (index < count - 1) {
						std::string optvalue = args[index + 1];
						opts[std::string(1, ch)] = optvalue;
						index += 2;
					}
					else {
						// missing parameter
						return false;
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
// load arguments from argc/argv
//---------------------------------------------------------------------
StringList LoadArgsFromArgcv(int argc, char *argv[], bool skipFirst)
{
	StringList args;
	for (int i = skipFirst ? 1 : 0; i < argc; i++) {
		args.push_back(std::string(argv[i]));
	}
	return args;
}


//---------------------------------------------------------------------
// convert number to human readable string: xx.x, xx.xK, xx.xM, etc.
// using 1024 as base, so 1K = 1024, 1M = 1024K = 1048576, etc. 
//---------------------------------------------------------------------
std::string PrettyNumber1(double value, bool spacing)
{
	// 1 K = 1024, 1 M = 1024 K = 1048576, etc.
	const char *units[] = {"", "K", "M", "G", "T"};
	double abs_value = (value < 0)? (-value) : value;
	const char *sign = (value < 0)? "-" : "";
	const char *space = spacing? " " : "";
	int unit_index = 0;
	int N = (int)(sizeof(units) / sizeof(units[0]));
	while (abs_value >= 1024.0 && unit_index < N - 1) {
		abs_value /= 1024.0;
		unit_index++;
	}
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%s%.1f%s%s", 
			sign, abs_value, space, units[unit_index]);
	return std::string(buffer);
}


//---------------------------------------------------------------------
// convert number to human readable string: xx, xxxK, xxxM, etc.
// using 1000 as base, so 1K = 1000, 1M = 1000K = 1000000, etc.
//---------------------------------------------------------------------
std::string PrettyNumber2(double value, bool spacing)
{
	// 1 K = 1000, 1 M = 1000 K = 1000000, etc.
	const char *units[] = {"", "K", "M", "G", "T"};
	double abs_value = (value < 0)? (-value) : value;
	const char *sign = (value < 0)? "-" : "";
	const char *space = spacing? " " : "";
	int unit_index = 0;
	int N = (int)(sizeof(units) / sizeof(units[0]));
	while (abs_value >= 1000.0 && unit_index < N - 1) {
		abs_value /= 1000.0;
		unit_index++;
	}
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%s%.0f%s%s", 
			sign, abs_value, space, units[unit_index]);
	return std::string(buffer);
}



//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



