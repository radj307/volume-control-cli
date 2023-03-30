#include "rc/version.h"
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
			<< "    - Session Identifier           (SUID)       Selects any audio session with the given Session Identifier." << '\n'
			<< "    - Session Instance Identifier  (SGUID)      Selects a specific audio session using its Session Instance Identifier." << '\n'
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

// Define exception type "except_showhelp":
$DefineExcept(showhelp)

// Globals:
static bool quiet{ false };
static bool extended{ false };

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
size_t MARGIN_WIDTH{ 12ull };

// Forward Declarations:
inline std::string getTargetAndValidateParams(const opt3::ArgManager&);
inline EDataFlow getTargetDataFlow(const opt3::ArgManager&);
inline void handleVolumeArgs(const opt3::ArgManager&, const vccli::Volume*);
inline void handleMuteArgs(const opt3::ArgManager&, const vccli::Volume*);


/**
 * @struct	VolumeObjectPrinter
 * @brief	Stream functor that pretty-prints a vccli::Volume object.
 */
struct VolumeObjectPrinter {
	vccli::Volume* obj;

	/**
	 * @brief		Creates a new VolumeObjectPrinter instance with the given Volume object pointer.
	 * @param obj	A pointer to a valid Volume object to print.
	 */
	constexpr VolumeObjectPrinter(vccli::Volume* obj) : obj{ obj } {}

	friend std::ostream& operator<<(std::ostream& os, const VolumeObjectPrinter& p)
	{
		if (p.obj) {
			const bool is_session{ p.obj->is_derived_type<vccli::ApplicationVolume>() }, is_device{ !is_session };

			if (quiet) {
				if (extended) {
					os
						<< (is_session ? 'P' : 'D') << "NAME: " << p.obj->resolved_name << '\n'
						<< (is_session ? "P" : "DGU") << "ID: " << p.obj->identifier << '\n'
						<< "TYPENAME: " << p.obj->type_name().value() << '\n'
						<< "DATAFLOW: " << p.obj->getFlowTypeName() << '\n'
						<< "VOLUME: " << p.obj->getVolumeScaled() << '\n'
						<< "IS_MUTED: " << std::boolalpha << p.obj->getMuted() << std::noboolalpha << '\n'
						;
					if (is_session) {
						auto* app = (vccli::ApplicationVolume*)p.obj;
						os
							<< "SUID: " << app->sessionIdentifier << '\n'
							<< "SGUID: " << app->sessionInstanceIdentifier << '\n';
					}
					else if (is_device)
						os << "IS_DEFAULT: " << std::boolalpha << ((vccli::EndpointVolume*)p.obj)->isDefault << std::noboolalpha << '\n';
				}
				else os << p.obj->type_name().value_or("null");
			}
			else {
				const auto& typecolor{ is_session ? COLOR::SESSION : COLOR::DEVICE };
				os
					<< "              " << colors(typecolor) << p.obj->resolved_name << colors() << '\n'
					<< "Typename:     " << colors(typecolor) << p.obj->type_name().value_or("null") << colors();
				if (is_device && ((vccli::EndpointVolume*)p.obj)->isDefault) os << ' ' << colors(COLOR::LOWLIGHT) << "(Default)" << colors() << '\n';
				if (is_session)
					os << '\n'
					<< "PID:          " << colors(COLOR::LOWLIGHT) << p.obj->identifier << colors() << '\n';
				os
					<< "Direction:    " << colors(p.obj->flow_type == EDataFlow::eRender ? COLOR::OUTPUT : COLOR::INPUT) << p.obj->getFlowTypeName() << colors() << '\n'
					<< "Volume:       " << colors(COLOR::VALUE) << p.obj->getVolumeScaled() << colors() << '\n'
					<< "Muted:        " << colors(COLOR::VALUE) << std::boolalpha << p.obj->getMuted() << std::noboolalpha << colors() << '\n'
					;

				if (extended) {
					if (is_session) {
						auto* app{ (vccli::ApplicationVolume*)p.obj };
						os
							<< "Session ID:   " << colors(COLOR::VALUE) << app->sessionIdentifier << colors() << '\n'
							<< "Instance ID:  " << colors(COLOR::VALUE) << app->sessionInstanceIdentifier << colors() << '\n'
							;
					}
				}
			}
		}
		return os;
	}
};

namespace vccli_operators {
	inline constexpr auto SEP{ ';' };
	inline constexpr auto COLSZ_DNAME{ 30 };
	inline constexpr auto COLSZ_DGUID{ 57 };
	inline constexpr auto COLSZ_IO{ 9 };
	inline constexpr auto COLSZ_DEFAULT{ 9 };

	inline std::ostream& operator<<(std::ostream& os, const vccli::DeviceInfo& di)
	{ // DEVICE INFO
		using namespace vccli;

		if (quiet) {
			os
				<< di.dname << SEP
				<< DataFlowToString(di.flow) << SEP
				<< std::boolalpha << di.isDefault << std::noboolalpha;
			if (extended)
				os << SEP << di.dguid;
		}
		else {
			const auto& flow_s{ DataFlowToString(di.flow) };
			const auto& def_s{ str::stringify(std::boolalpha, di.isDefault) };
			os
				<< colors(COLOR::DEVICE) << di.dname << colors() << indent(COLSZ_DNAME, di.dname.size())
				<< colors(COLOR::VALUE) << flow_s << colors() << indent(COLSZ_IO, flow_s.size())
				<< colors(COLOR::LOWLIGHT) << def_s << colors();
			if (extended) os
				<< indent(COLSZ_DEFAULT, def_s.size()) << di.dguid;
		}
		return os;
	}

	inline constexpr auto COLSZ_PNAME{ 24 };
	inline constexpr auto COLSZ_PID{ 10 };

	inline std::ostream& operator<<(std::ostream& os, const vccli::ProcessInfo& pi)
	{ // PROCESS INFO
		using namespace vccli;

		if (quiet) {
			os
				<< pi.pid << SEP
				<< pi.pname << SEP
				;
		}
		else {
			const auto& flow_s{ DataFlowToString(pi.flow) };
			const auto& pid_s{ std::to_string(pi.pid) };
			os
				<< '[' << colors(COLOR::SESSION) << pid_s << colors() << ']' << indent(COLSZ_PID, pid_s.size() + 2)
				<< colors(COLOR::SESSION) << pi.pname << colors() << indent(COLSZ_PNAME, pi.pname.size())
				;
		}

		os << static_cast<DeviceInfo>(pi);

		if (extended) {
			if (quiet) os
				<< SEP
				<< pi.suid << SEP
				<< pi.sguid
				;
			else os
				<< indent(2)
				<< colors(COLOR::DEVICE) << pi.dguid << colors() << SEP
				<< pi.suid << SEP
				<< pi.sguid
				;
		}
		return os;
	}

	template<std::derived_from<vccli::basic_info> T>
	struct InfoLister {
		std::vector<T> vec;

		InfoLister(std::vector<T>&& vec) : vec{ std::forward<std::vector<T>>(vec) } {}

		friend std::ostream& operator<<(std::ostream& os, const InfoLister<T>& p)
		{
			std::vector<std::string> columns;

			if (quiet) {
				if constexpr (std::same_as<T, vccli::DeviceInfo>) {
					os << "DNAME" << SEP << "I/O" << SEP << "IS_DEFAULT";
					if (extended) os << SEP << "DGUID";
				}
				else if constexpr (std::same_as<T, vccli::ProcessInfo>) {
					os << "PID" << SEP << "PNAME" << SEP << "DNAME" << SEP << "I/O" << SEP << "IS_DEFAULT";
					if (extended) os << SEP << "DGUID" << SEP << "SUID" << SEP << "SGUID";
				}
			}
			else {
				if constexpr (std::same_as<T, vccli::DeviceInfo>) {
					os
						<< colors(COLOR::HEADER)
						<< "Device Name (DNAME)" << indent(COLSZ_DNAME - 19)
						<< "I/O" << indent(COLSZ_IO - 3)
						<< "Default";
					;
					if (extended) os << indent(COLSZ_DEFAULT - 7) << "Device ID (DGUID)";
				}
				else if constexpr (std::same_as<T, vccli::ProcessInfo>) {
					os
						<< colors(COLOR::HEADER)
						<< "PID" << indent(COLSZ_PID - 3)
						<< "Process Name (PNAME)" << indent(COLSZ_PNAME - 20)
						<< "Device Name (DNAME)" << indent(COLSZ_DNAME - 19)
						<< "I/O" << indent(COLSZ_IO - 3)
						<< "Default";
					if (extended) os << indent(COLSZ_DEFAULT - 7)
						<< "Device ID (DGUID)" << indent(COLSZ_DGUID - 17)
						<< "Session ID" << SEP
						<< "Instance ID";
					os << colors();
				}
			}
			os << "\n\n";

			for (const auto& obj : p.vec)
				os << obj << '\n';

			return os;
		}
	};
}

template<std::derived_from<vccli::basic_info> T>
vccli_operators::InfoLister<T> make_printable_list(std::vector<T>&& vec)
{
	return vccli_operators::InfoLister<T>{ std::forward<std::vector<T>>(vec) };
}



int main(const int argc, char** argv)
{
	using namespace vccli;
	int rc{ 0 };
	try {
		opt3::ArgManager args{ argc, argv,
			opt3::make_template(opt3::CaptureStyle::Optional, 'v', "volume"),
			opt3::make_template(opt3::CaptureStyle::Optional, 'm', "mute", "muted", "is-muted"),
			opt3::make_template(opt3::CaptureStyle::Required, 'I', "increment"),
			opt3::make_template(opt3::CaptureStyle::Required, 'D', "decrement"),
			opt3::make_template(opt3::CaptureStyle::Required, 'd', "dev"),
		};

		// handle important general args
		quiet = args.check_any<opt3::Flag, opt3::Option>('q', "quiet");
		colors.setActive(!quiet && !args.check_any<opt3::Flag, opt3::Option>('n', "no-color"));
		extended = args.check_any<opt3::Flag, opt3::Option>('e', "extended");

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
		const auto& targetControllers{ AudioAPI::getObjects(target, args.check_any<opt3::Flag, opt3::Option>('f', "fuzzy"), flow) };

		if (targetControllers.empty())
			throw make_exception(
				"Couldn't locate anything matching the given search term!\n",
				indent(10), colors(COLOR::HEADER), "Search Term", colors(), ":    ", colors(COLOR::ERR), target, colors(), '\n',
				indent(10), colors(COLOR::HEADER), "Device Filter", colors(), ":  ", colors(COLOR::ERR), DataFlowToString(flow), colors()
			);

		const bool
			listSessions{ args.check_any<opt3::Flag, opt3::Option>('l', "list") },
			listDevices{ args.check_any<opt3::Flag, opt3::Option>('L', "list-dev") };

		// -Q | --query
		if (args.check_any<opt3::Flag, opt3::Option>('Q', "query")) {
			bool fst{ true };
			for (const auto& it : targetControllers) {
				if (fst) fst = false;
				else std::cout << '\n';
				std::cout << VolumeObjectPrinter(it.get());
			}
		}
		// list
		else if (listSessions || listDevices) {
			// -l | --list
			if (listSessions) {
				std::cout << make_printable_list(AudioAPI::GetAllAudioProcessesSorted(flow));
				if (listDevices) std::cout << '\n';
			}
			// -L | --list-dev
			if (listDevices)
				std::cout << make_printable_list(AudioAPI::GetAllAudioDevicesSorted(flow));
		}
		// Non-blocking options:
		else {
			for (const auto& it : targetControllers) {
				// Handle Volume Args:
				handleVolumeArgs(args, it.get());

				// Handle Mute Args:
				handleMuteArgs(args, it.get());
			}
		}

	} catch (const showhelp& ex) {
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
	if (!params.empty()) throw make_custom_exception<showhelp>("Unexpected Arguments:  ", str::join(params, ", "));
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
				if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << static_cast<int>(tgtVolume) << colors() << '\n';
			}
			else {
				controller->setVolumeScaled(tgtVolume);
				if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << static_cast<int>(tgtVolume) << colors() << '\n';
			}
		}
		else {
			// Get
			if (!quiet) std::cout << "Volume:" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE);
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
			if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::WARN) << "true" << colors() << '\n';
		}
		else {
			controller->mute();
			if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE) << "true" << colors() << '\n';
		}
	}
	else if (unmute) {
		if (controller->getMuted() == false) {
			if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::WARN) << "false" << colors() << '\n';
		}
		else {
			controller->unmute();
			if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE) << "false" << colors() << '\n';
		}
	}

	if (const auto& arg{ args.get_any<opt3::Flag, opt3::Option>('m', "is-muted") }; arg.has_value()) {
		if (const auto& captured{ arg.value().getValue() }; captured.has_value()) {
			if (mute || unmute) throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-M", colors(), '|', colors(COLOR::ERR), "--is-muted", colors(COLOR::ERR), " && (", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " || ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors(), ')');
			// Set
			const auto& value{ str::trim(captured.value()) };
			if (str::equalsAny<true>(value, "true", "1", "on")) {
				if (controller->getMuted() == true) {
					if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::WARN) << "true" << colors() << '\n';
				}
				else {
					controller->mute();
					if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE) << "true" << colors() << '\n';
				}
			}
			else if (str::equalsAny<true>(value, "false", "0", "off")) {
				if (controller->getMuted() == false) {
					if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::WARN) << "false" << colors() << '\n';
				}
				else {
					controller->unmute();
					if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE) << "false" << colors() << '\n';
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