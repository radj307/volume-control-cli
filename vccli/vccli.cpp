﻿#include "rc/version.h"
#include "AudioAPI.hpp"

#include <TermAPI.hpp>
#include <opt3.hpp>

#include <typeinfo>

struct PrintHelp {
	friend std::ostream& operator<<(std::ostream& os, const PrintHelp& h)
	{
		return os
			<< "vccli v" << vccli_VERSION_EXTENDED << '\n'
			<< "  Volume Control CLI allows you to control audio endpoints (Devices) & audio sessions (Sessions) from the commandline.\n"
			<< '\n'
			<< "USAGE:\n"
			<< "  vccli [TARGET] [OPTIONS]" << '\n'
			<< '\n'
			<< "  The '[TARGET]' field determines which device or session to target with commands, and accepts a variety of inputs:" << '\n'
			<< "    - Device ID                    (DGUID)      Selects an audio device using the string representation of its GUID." << '\n'
			<< "    - Device Name                  (DNAME)      Selects an audio device using its controller interface's name." << '\n'
			<< "    - Process ID                   (PID)        Selects a specific audio session using a known process ID number." << '\n'
			<< "    - Process Name                 (PNAME)      Selects a specific audio session using a process name." << '\n'
			<< "    - Session Identifier           (SGUID)      Selects any audio session with the given Session Identifier." << '\n'
			<< "    - Session Instance Identifier  (SIGUID)     Selects a specific audio session using its Session Instance Identifier." << '\n'
			<< "    - Blank                                     Gets the default audio endpoint for the type specified by '-d'|'--dev'." << '\n'
			<< '\n'
			<< "  Certain device endpoint names (DNAME) that are built-in to Windows contain trailing whitespace, such as" << '\n'
			<< "   'USB Audio Codec '; keep this in mind when searching for devices by name, and/or use the ('-f'|'--fuzzy') option." << '\n'
			<< '\n'
			<< "OPTIONS:\n"
			<< "  -h, --help                   Shows this help display, then exits." << '\n'
			<< "      --version                Prints the current version number, then exits." << '\n'
			<< "  -q, --quiet                  Show only minimal console output for getters; don't show any console output for setters." << '\n'
			<< "  -n, --no-color               Disables ANSI color sequences; this option is implied when '-q'|'--quiet' is specified." << '\n'
			<< "  -d, --dev <i|o>              Selects input or output devices.  When targeting an endpoint, this determines the type" << '\n'
			<< "                                of device to use; when targeting a session, limits the search to devices of this type." << '\n'
			<< "  -f, --fuzzy                  Fuzzy search; allows partial matches instead of requiring a full match." << '\n'
			<< "  -e, --extended               Shows additional fields when used with the query or list options." << '\n'
			<< '\n'
			<< "OPTIONS - Modes, Getters, & Setters:\n"
			<< "  -Q, --query                  Shows information about the specified TARGET if it exists; otherwise shows an error." << '\n'
			<< "  -l, --list                   Prints a list (sorted by PID) of all processes with an active audio session, then exits." << '\n'
			<< "  -L, --list-dev               Prints a list of all audio endpoints that aren't unplugged or disabled, then exits." << '\n'
			<< "  -v, --volume [0-100]         Gets or sets (when a number is specified) the volume of the target." << '\n'
			<< "  -I, --increment <0-100>      Increments the volume of the target by the specified number." << '\n'
			<< "  -D, --decrement <0-100>      Decrements the volume of the target by the specified number." << '\n'
			<< "  -m, --is-muted [true|false]  Gets or sets (when a boolean is specified) the mute state of the target." << '\n'
			<< "  -M, --mute                   Mutes the target.    (Equivalent to '-m=true'|'--is-muted=true')" << '\n'
			<< "  -U, --unmute                 Unmutes the target.  (Equivalent to '-m=false'|'--is-muted=false')" << '\n'
			;
	}
};

$DEFINE_EXCEPT(showhelp)

// Globals:
inline static bool quiet{ false };
inline static bool extended{ false };

enum class COLOR {
	HEADER,
	VALUE,
	HIGHLIGHT,
	LOWLIGHT,
	WARN,
	ERR,
	DEVICE,
	SESSION,
	INPUT,
	OUTPUT,
};
term::palette<COLOR> colors{
	std::make_pair(COLOR::HEADER, term::setcolor(term::make_sequence(color::FormatFlag::Bold))),
	std::make_pair(COLOR::VALUE, color::setcolor(1, 4, 1)),
	std::make_pair(COLOR::HIGHLIGHT, color::cyan),
	std::make_pair(COLOR::LOWLIGHT, color::light_gray),
	std::make_pair(COLOR::WARN, color::yellow),
	std::make_pair(COLOR::ERR, color::orange),
	std::make_pair(COLOR::DEVICE, color::setcolor(term::make_sequence(color::setcolor(color::lighter_purple), color::FormatFlag::Bold))),
	std::make_pair(COLOR::SESSION, color::light_blue),
	std::make_pair(COLOR::INPUT, color::pink),
	std::make_pair(COLOR::OUTPUT, color::setcolor(4, 4, 0)),
};
size_t MARGIN_WIDTH{ 20ull };

// Forward Declarations:
inline std::string getTargetAndValidateParams(const opt3::ArgManager&);
inline EDataFlow getTargetDataFlow(const opt3::ArgManager&);
inline void handleVolumeArgs(const opt3::ArgManager&, const vccli::Volume*);
inline void handleMuteArgs(const opt3::ArgManager&, const vccli::Volume*);


struct VolumeObjectPrinter {
	vccli::Volume* obj;

	constexpr VolumeObjectPrinter(vccli::Volume* obj) : obj{ obj } {}

	friend std::ostream& operator<<(std::ostream& os, const VolumeObjectPrinter& p)
	{
		if (p.obj) {
			const bool is_session_not_device{ p.obj->is_derived_type<vccli::ApplicationVolume>() };

			if (quiet) {
				if (extended) {
					os
						<< (is_session_not_device ? 'P' : 'D') << "NAME: " << p.obj->resolved_name << '\n'
						<< (is_session_not_device ? "P" : "DGU") << "ID: " << p.obj->identifier << '\n'
						<< "TYPENAME: " << p.obj->type_name().value() << '\n'
						<< "DATAFLOW: " << p.obj->getFlowTypeName() << '\n'
						<< "VOLUME: " << p.obj->getVolumeScaled() << '\n'
						<< "IS_MUTED: " << std::boolalpha << p.obj->getMuted() << std::noboolalpha << '\n'
						;
					if (p.obj->is_derived_type<vccli::ApplicationVolume>()) {
						auto* app = (vccli::ApplicationVolume*)p.obj;
						os
							<< "SID: " << app->sessionIdentifier << '\n'
							<< "SIID: " << app->sessionInstanceIdentifier << '\n';
					}
					else if (p.obj->is_derived_type<vccli::EndpointVolume>())
						os << "DEFAULT: " << std::boolalpha << ((vccli::EndpointVolume*)p.obj)->isDefault << std::noboolalpha << '\n';
				}
				else os << p.obj->type_name().value_or("null");
			}
			else {
				const auto& typecolor{ is_session_not_device ? COLOR::SESSION : COLOR::DEVICE };
				os
					<< "             " << colors(typecolor) << p.obj->resolved_name << colors() << '\n'
					<< "Typename:    " << colors(typecolor) << p.obj->type_name().value_or("null") << colors();
				if (!is_session_not_device && ((vccli::EndpointVolume*)p.obj)->isDefault) os << ' ' << colors(COLOR::LOWLIGHT) << "(Default)" << colors();
				os << '\n'
					<< "Direction:   " << colors(p.obj->flow_type == EDataFlow::eRender ? COLOR::OUTPUT : COLOR::INPUT) << p.obj->getFlowTypeName() << colors() << '\n'
					<< "Volume:      " << colors(COLOR::VALUE) << p.obj->getVolumeScaled() << colors() << '\n'
					<< "Muted:       " << colors(COLOR::VALUE) << std::boolalpha << p.obj->getMuted() << std::noboolalpha << colors() << '\n'
					;

				if (extended) {
					os
						<< ""
						;
				}
			}
		}
		return os;
	}
};


int main(const int argc, char** argv)
{
	using namespace vccli;
	int rc{ 0 };
	try {
		using namespace opt3_literals;
		opt3::ArgManager args{ argc, argv,
			'v'_optcap,
			"volume"_optcap,
			'm'_optcap,
			"is-muted"_optcap,
			'I'_reqcap,
			"increment"_reqcap,
			'D'_reqcap,
			"decrement"_reqcap,
			'd'_reqcap,
			"dev"_reqcap
		};

		// handle important general blocking args
		quiet = args.check_any<opt3::Flag, opt3::Option>('q', "quiet");
		colors.setActive(!quiet && !args.check_any<opt3::Flag, opt3::Option>('n', "no-color"));
		extended = args.check_any<opt3::Flag, opt3::Option>('e', "extended");

		if (extended) MARGIN_WIDTH += 10;

		if (args.empty() || args.check_any<opt3::Flag, opt3::Option>('h', "help")) {
			std::cout << PrintHelp{};
			return 0;
		}
		else if (args.checkopt("version")) {
			if (!quiet) std::cout << "vccli v";
			std::cout << vccli_VERSION_EXTENDED << '\n';
			return 0;
		}

		// Get the target string
		const std::string target{ getTargetAndValidateParams(args) };
		EDataFlow flow{ getTargetDataFlow(args) };

		// Initialize Windows API
		if (const auto& hr{ CoInitializeEx(NULL, COINIT::COINIT_MULTITHREADED) }; hr != S_OK)
			throw make_exception("Failed to initialize COM interface with error code ", hr, ": '", GetErrorMessageFrom(hr), "'!");

		// Get controller:
		const auto& targetController{ AudioAPI::getObject(target, args.check_any<opt3::Flag, opt3::Option>('f', "fuzzy"), flow) };

		if (targetController.get() == nullptr)
			throw make_exception(
				"Couldn't locate anything matching the given search term!\n",
				indent(10), colors(COLOR::HEADER), "Search Term", colors(), ":    ", colors(COLOR::ERR), target, colors(), '\n',
				indent(10), colors(COLOR::HEADER), "Device Filter", colors(), ":  ", colors(COLOR::ERR), DataFlowToString(flow), colors()
			);

		// -Q | --query
		if (args.check_any<opt3::Flag, opt3::Option>('Q', "query")) {
			std::cout << VolumeObjectPrinter(targetController.get()) << '\n';/*
			const auto& typeName{ targetController->type_name() };
			if (!quiet) {
				std::cout << "Name:" << indent(MARGIN_WIDTH, 6) << colors(COLOR::HIGHLIGHT) << targetController->resolved_name << colors() << '\n';
				std::cout
					<< "Type:" << indent(MARGIN_WIDTH, 6) << colors(COLOR::VALUE) << typeName.value_or("Undefined") << colors();
				if (targetController->is_derived_type<EndpointVolume>()) {
					auto* dev = (EndpointVolume*)targetController.get();
					std::cout << (dev->isDefault ? " (Default)" : "");
				}
				std::cout
					<< '\n'
					<< "DataFlow:" << indent(MARGIN_WIDTH, 10) << colors(COLOR::VALUE) << targetController->getFlowTypeName() << colors() << '\n'
					<< "Volume:" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << str::stringify(std::fixed, std::setprecision(0), targetController->getVolumeScaled()) << colors() << '\n'
					<< "Muted:" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE) << str::stringify(std::boolalpha, targetController->getMuted()) << colors() << '\n';
				if (extended) {
					if (targetController->is_derived_type<ApplicationVolume>()) {
						auto* app = (ApplicationVolume*)targetController.get();
						std::cout
							<< "Endpoint Name (DNAME):" << indent(MARGIN_WIDTH, 23) << colors(COLOR::VALUE) << AudioAPI::getDeviceName(app->dev_id) << colors() << '\n'
							<< "Endpoint GUID (DGUID):" << indent(MARGIN_WIDTH, 23) << colors(COLOR::VALUE) << app->dev_id << colors() << '\n'
							<< "Session ID    (SID):" << indent(MARGIN_WIDTH, 21) << colors(COLOR::VALUE) << app->sessionIdentifier << colors() << '\n'
							<< "Instance ID   (SIID):" << indent(MARGIN_WIDTH, 22) << colors(COLOR::VALUE) << app->sessionInstanceIdentifier << colors() << '\n'
							;
					}
				}
			}
			else if (extended && typeName.has_value()) {
				std::cout
					<< typeName.value() << '\n'
					<< targetController->getVolumeScaled() << '\n'
					<< str::stringify(std::boolalpha, targetController->getMuted()) << '\n'
					;
			}
			else std::cout << typeName.value_or("null") << '\n';*/

		}
		// -l | --list
		else if (args.check_any<opt3::Flag, opt3::Option>('l', "list")) {
			if (!quiet) {
				std::cout
					<< colors(COLOR::HEADER)
					<< "PID" << indent(MARGIN_WIDTH, 3) << "Process Name (PNAME)" << colors() << '\n'
					<< indent(MARGIN_WIDTH + 21, 0, '-') << '\n'
					;
			}
			for (const auto& [pid, pname] : AudioAPI::GetAllAudioProcessesSorted(flow)) {
				std::string pid_s{ std::to_string(pid) };
				if (!quiet) std::cout << '[' << colors(COLOR::VALUE);
				std::cout << pid_s;
				if (!quiet) std::cout << colors() << ']' << indent(MARGIN_WIDTH, pid_s.size() + 2ull);
				else std::cout << ';';
				std::cout << pname << '\n';
			}
		}
		// -L | --list-dev
		else if (args.check_any<opt3::Flag, opt3::Option>('L', "list-dev")) {
			const auto& devicesSorted{ AudioAPI::GetAllAudioDevicesSorted(flow) };
			const size_t maxTypeNameLen{ 23ull + 3ull };
			size_t longest_name_len{ 0ull };
			for (const auto& it : devicesSorted) {
				if (const auto& size{ it.name.size() }; size > longest_name_len)
					longest_name_len = size;
			}
			longest_name_len += 3;
			if (!quiet) {
				std::cout
					<< "Device Name (DNAME)" << indent(longest_name_len + 2, 19ull) << "Device Type";
				if (extended) std::cout << indent(maxTypeNameLen, 11ull) << "Device ID (DGUID)" << '\n';
				static constexpr auto DGUID_LENGTH{ 55 };
				std::cout << indent(longest_name_len + maxTypeNameLen + 2 + (extended ? DGUID_LENGTH : 0), 0, '-') << '\n';
			}
			for (const auto& it : devicesSorted) {
				const auto& typeName{ it.type_name().value_or("Undefined") };
				if (!quiet) std::cout << '[' << colors(COLOR::HIGHLIGHT);
				std::cout << it.name;
				if (!quiet) std::cout << colors() << ']' << indent(longest_name_len, it.name.size()) << colors(COLOR::VALUE);
				else std::cout << ';';
				std::cout << typeName;
				if (!quiet) std::cout << colors();
				if (extended) {
					if (!quiet) std::cout << indent(maxTypeNameLen, typeName.size());
					else std::cout << ';';
					std::cout << it.id << '\n';
				}
			}
		}
		// Non-blocking options:
		else {
			// Handle Volume Args:
			handleVolumeArgs(args, targetController.get());

			// Handle Mute Args:
			handleMuteArgs(args, targetController.get());
		}

	} catch (const $EXCEPT(showhelp)& ex) {
		std::cerr << PrintHelp{} << '\n' << colors.get_fatal() << ex.what() << '\n';
		rc = 1;
	} catch (const std::exception& ex) {
		std::cerr << colors.get_fatal() << ex.what() << '\n';
		rc = 1;
	} catch (...) {
		std::cerr << colors.get_fatal() << "An undefined exception occurred!" << '\n';
		rc = 1;
	}
	// Uninitialize Windows API
	CoUninitialize();
	return rc;
}

// Definitions:
inline std::string getTargetAndValidateParams(const opt3::ArgManager& args)
{
	auto params{ args.getv_all<opt3::Parameter>() };
	if (params.empty()) return{};
	// Copy & pop the first parameter from the vector
	std::string target{ params.front() };
	params.erase(params.begin());
	// If there are any additional parameters, throw
	if (!params.empty()) throw make_custom_exception<$EXCEPT(showhelp)>("Unexpected Arguments:  ", str::join(params, ", "));
	return target;
}

inline EDataFlow getTargetDataFlow(const opt3::ArgManager& args)
{
	if (const auto& dev{ args.getv_any<opt3::Flag, opt3::Option>('d', "dev", "device") }; dev.has_value()) {
		std::string v{ str::tolower(dev.value()) };
		if (str::equalsAny<true>(v, "i", "in", "input", "rec", "record", "recording"))
			return EDataFlow::eCapture;
		else if (str::equalsAny<true>(v, "o", "out", "output", "play", "playback"))
			return EDataFlow::eRender;
		else throw make_exception("Invalid Device Flow State:  ", v, " ; (expected i/in/input/rec/record/recording || o/out/output/play/playback)!");
	}
	else return EDataFlow::eAll;
}
inline void handleVolumeArgs(const opt3::ArgManager& args, const vccli::Volume* controller)
{
	const auto& increment{ args.getv_any<opt3::Flag, opt3::Option>('I', "increment") }, & decrement{ args.getv_any<opt3::Flag, opt3::Option>('D', "decrement") };
	if (increment.has_value() && decrement.has_value())
		throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-I", colors(), '|', colors(COLOR::ERR), "--increment", colors(), " && ", colors(COLOR::ERR), "-D", colors(), '|', colors(COLOR::ERR), "--decrement", colors());
	else if (increment.has_value()) {
		const auto& value{ increment.value() };
		if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
			throw make_exception("Invalid Number Specified:  ", value);
		if (controller->getVolumeScaled() == 100.0f) {
			if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 10ull) << colors(COLOR::WARN) << "100" << colors() << '\n';
		}
		else {
			controller->incrementVolume(str::stof(value) / 100.0f);
			if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::VALUE) << static_cast<int>(controller->getVolumeScaled()) << colors() << " (+" << colors(COLOR::VALUE) << value << colors() << ')' << '\n';
		}
	}
	else if (decrement.has_value()) {
		const auto& value{ decrement.value() };
		if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
			throw make_exception("Invalid Number Specified:  ", value);
		if (controller->getVolumeScaled() == 0.0f) {
			if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 10ull) << colors(COLOR::WARN) << "0" << colors() << '\n';
		}
		else {
			controller->decrementVolume(str::stof(value) / 100.0f);
			if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::VALUE) << static_cast<int>(controller->getVolumeScaled()) << colors() << " (-" << colors(COLOR::VALUE) << value << colors() << ')' << '\n';
		}
	}

	if (const auto& arg{ args.get_any<opt3::Flag, opt3::Option>('v', "volume") }; arg.has_value()) {
		if (const auto& captured{ arg.value().getValue() }; captured.has_value()) {
			// Set
			const auto& value{ captured.value() };
			if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
				throw make_exception("Invalid Number Specified:  ", value);
			float tgtVolume{ str::stof(captured.value()) };
			if (tgtVolume > 100.0f)
				tgtVolume = 100.0f;
			else if (tgtVolume < 0.0f)
				tgtVolume = 0.0f;
			if (controller->getVolumeScaled() == tgtVolume) {
				if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 10ull) << colors(COLOR::WARN) << static_cast<int>(tgtVolume) << colors() << '\n';
			}
			else {
				controller->setVolumeScaled(tgtVolume);
				if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::VALUE) << static_cast<int>(tgtVolume) << colors() << '\n';
			}
		}
		else {
			// Get
			if (!quiet) std::cout << "Volume:" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE);
			std::cout << str::stringify(std::fixed, std::setprecision(0), controller->getVolume() * 100.0f);
			if (!quiet) std::cout << colors() << '\n';
		}
	}
}
inline void handleMuteArgs(const opt3::ArgManager& args, const vccli::Volume* controller)
{
	const bool
		mute{ args.check_any<opt3::Flag, opt3::Option>('M', "mute") },
		unmute{ args.check_any<opt3::Flag, opt3::Option>('U', "unmute") };

	if (mute && unmute)
		throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " && ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors());
	else if (mute) {
		if (controller->getMuted() == true) {
			if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "true" << colors() << '\n';
		}
		else {
			controller->mute();
			if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << "true" << colors() << '\n';
		}
	}
	else if (unmute) {
		if (controller->getMuted() == false) {
			if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "false" << colors() << '\n';
		}
		else {
			controller->unmute();
			if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << "false" << colors() << '\n';
		}
	}

	if (const auto& arg{ args.get_any<opt3::Flag, opt3::Option>('m', "is-muted") }; arg.has_value()) {
		if (const auto& captured{ arg.value().getValue() }; captured.has_value()) {
			if (mute || unmute) throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-M", colors(), '|', colors(COLOR::ERR), "--is-muted", colors(COLOR::ERR), " && (", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " || ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors(), ')');
			// Set
			const auto& value{ str::trim(captured.value()) };
			if (str::equalsAny<true>(value, "true", "1", "on")) {
				if (controller->getMuted() == true) {
					if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "true" << colors() << '\n';
				}
				else {
					controller->mute();
					if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << "true" << colors() << '\n';
				}
			}
			else if (str::equalsAny<true>(value, "false", "0", "off")) {
				if (controller->getMuted() == false) {
					if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "false" << colors() << '\n';
				}
				else {
					controller->unmute();
					if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << "false" << colors() << '\n';
				}
			}
			else throw make_exception("Invalid Argument Specified:  '", colors(COLOR::ERR), captured.value(), colors(), "';  Expected a boolean value ('", colors(COLOR::ERR), "true", colors(), "'/'", colors(COLOR::ERR), "false", colors(), "')!");
		}
		else {
			// Get
			if (!quiet) std::cout << "Is Muted:" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::VALUE);
			std::cout << str::stringify(std::boolalpha, controller->getMuted());
			if (!quiet) std::cout << colors() << '\n';
		}
	}
}