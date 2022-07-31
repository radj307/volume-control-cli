#include "rc/version.h"
#include "AudioAPI.hpp"

#include <TermAPI.hpp>
#include <opt3.hpp>

struct PrintHelp {
	friend std::ostream& operator<<(std::ostream& os, const PrintHelp& h)
	{
		return os
			<< "vccli v" << vccli_VERSION_EXTENDED << '\n'
			<< "  Windows commandline utility that can mute/unmute/change the volume of specific processes.\n"
			<< '\n'
			<< "USAGE:\n"
			<< "  vccli [TARGET] [OPTIONS]" << '\n'
			<< '\n'
			<< "  The '[TARGET]' field accepts a variety of inputs:" << '\n'
			<< "   - Process ID    (PID)        Selects a specific audio session using a known process ID number." << '\n'
			<< "   - Process Name  (PNAME)      Selects a specific audio session using a process name." << '\n'
			<< "   - Device ID     (DGUID)      Selects an audio endpoint using the string representation of its GUID." << '\n'
			<< "   - Device Name   (DNAME)      Selects an audio endpoint using its interface name." << '\n'
			<< "   - Blank                      Gets the default audio endpoint. (Use '--dev' to select input or output devices.)" << '\n'
			<< '\n'
			<< "  Note that 'Device' refers to the device interface, such as 'USB Audio CODEC'; NOT the device visible in Sounds." << '\n'
			<< '\n'
			<< "OPTIONS:\n"
			<< "  -h, --help                   Shows this help display, then exits." << '\n'
			<< "      --version                Prints the current version number, then exits." << '\n'
			<< "  -q, --quiet                  Show only minimal console output for getters; don't show any console output for setters." << '\n'
			<< "  -n, --no-color               Disables ANSI color sequences; this option is implied when '-q'|'--quiet' is specified." << '\n'
			<< "      --dev <i|o>              Selects input or output devices.  When targeting an endpoint, this determines the type" << '\n'
			<< "                                of device to use; when targeting a session, limits the search to devices of this type." << '\n'
			<< "  -Q, --query                  Checks if the specified TARGET exists, then exits." << '\n'
			<< "  -l, --list                   Prints a list (sorted by PID) of all processes with an active audio session, then exits." << '\n'
			<< "  -v, --volume [0-100]         Gets or sets (when a number is specified) the volume of the target." << '\n'
			<< "  -I, --increment <0-100>      Increments the volume of the target by the specified number." << '\n'
			<< "  -D, --decrement <0-100>      Decrements the volume of the target by the specified number." << '\n'
			<< "  -m, --is-muted [true|false]  Gets or sets (when a boolean is specified) the mute state of the target." << '\n'
			<< "  -M, --mute                   Mutes the target.  (Equivalent to '--is-muted=true')" << '\n'
			<< "  -U, --unmute                 Unmutes the target.  (Equivalent to '--is-muted=false')" << '\n'
			;
	}
};

$DEFINE_EXCEPT(showhelp)

// Globals:
inline static bool quiet{ false };

enum class COLOR {
	HEADER,
	VALUE,
	HIGHLIGHT,
	WARN,
	ERR,
};
term::palette<COLOR> colors{
	std::make_pair(COLOR::HEADER, color::white),
	std::make_pair(COLOR::VALUE, color::green),
	std::make_pair(COLOR::HIGHLIGHT, color::cyan),
	std::make_pair(COLOR::WARN, color::yellow),
	std::make_pair(COLOR::ERR, color::red),
};
inline constexpr size_t MARGIN_WIDTH{ 13ull };

// Forward Declarations:
inline std::string getTargetAndValidateParams(const opt3::ArgManager&);
inline EDataFlow getDeviceDataFlow(const opt3::ArgManager&);
inline void handleVolumeArgs(const opt3::ArgManager&, const vccli::Volume*);
inline void handleMuteArgs(const opt3::ArgManager&, const vccli::Volume*);


int main(const int argc, char** argv)
{
	using namespace vccli;
	int rc{ 0 };
	try {
		using namespace opt3_literals;
		opt3::ArgManager args{ argc, argv,
			'h'_nocap,
			"help"_nocap,
			'v'_optcap,
			"volume"_optcap,
			'm'_optcap,
			"is-muted"_optcap,
			'I'_reqcap,
			"increment"_reqcap,
			'D'_reqcap,
			"decrement"_reqcap,
			"dev"_reqcap
		};

		// handle important general blocking args
		quiet = args.check_any<opt3::Flag, opt3::Option>('q', "quiet");
		colors.setActive(!quiet && !args.check_any<opt3::Flag, opt3::Option>('n', "no-color"));

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

		// Initialize Windows API
		if (const auto& hr{ CoInitializeEx(NULL, COINIT::COINIT_MULTITHREADED) }; hr != S_OK)
			throw make_exception("Failed to initialize COM interface with error code ", hr, ": '", GetErrorMessageFrom(hr), "'!");

		// Get controller:
		const auto& targetController{ AudioAPI::getObject(target, getDeviceDataFlow(args)) };

		// -Q | --query
		if (args.check_any<opt3::Flag, opt3::Option>('Q', "query")) {
			const bool isValid{ targetController->isValid() };
			const auto& typeName{ targetController->type_name() };
			if (!quiet) {
				const std::string head{ isValid ? "Found" : "Not Found" };
				std::cout << head << ':' << indent(MARGIN_WIDTH, head.size() + 2ull) << colors(isValid ? COLOR::HIGHLIGHT : COLOR::ERR) << targetController->resolved_name << colors() << '\n';
				if (isValid)
					std::cout
					<< "Type:" << indent(MARGIN_WIDTH, 6ull) << colors(COLOR::VALUE) << typeName.value_or("Undefined") << (target.empty() ? " (Default)" : "") << colors() << '\n'
					<< "Volume:" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::VALUE) << str::stringify(std::fixed, std::setprecision(0), targetController->getVolumeScaled()) << colors() << '\n'
					<< "Muted:" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::VALUE) << str::stringify(std::boolalpha, targetController->getMuted()) << colors() << '\n';
			}
			else std::cout << typeName.value_or("false") << '\n';
		}
		// -l | --list
		else if (args.check_any<opt3::Flag, opt3::Option>('l', "list")) {
			for (const auto& [pid, pname] : AudioAPI::GetAllAudioProcessesSorted(EDataFlow::eRender)) {
				std::string pid_s{ std::to_string(pid) };
				if (!quiet) std::cout << '[' << colors(COLOR::VALUE);
				std::cout << pid_s;
				if (!quiet) std::cout << colors() << ']' << indent(MARGIN_WIDTH, pid_s.size() + 2ull);
				std::cout << ' ' << pname << '\n';
			}
		}
		// Non-blocking options:
		else if (targetController->isValid()) {
			// Handle Volume Args:
			handleVolumeArgs(args, targetController.get());

			// Handle Mute Args:
			handleMuteArgs(args, targetController.get());
		}
		else throw make_exception("Couldn't Find Target:  '", target, "'!");

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

inline EDataFlow getDeviceDataFlow(const opt3::ArgManager& args)
{
	if (const auto& dev{ args.getv<opt3::Option>("dev") }; dev.has_value()) {
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