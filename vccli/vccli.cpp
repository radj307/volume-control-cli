#include "rc/version.h"
#include "AudioAPI.hpp"

#include <TermAPI.hpp>
#include <ParamsAPI2.hpp>

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
			<< "  The 'TARGET' field can be filled in with a Process ID (PID), ProcessName (PNAME), or DeviceID (DGUID)." << '\n'
			<< "  Target processes can be specified using either their ProcessID (PID) or their ProcessName (PNAME)." << '\n'
			<< "  Target device endpoints can be specified using either their GUID (DGUID) or by leaving blank for the default device." << '\n'
			<< "  " << '\n'
			<< '\n'
			<< "OPTIONS:\n"
			<< "  -h, --help                   Shows this help display, then exits." << '\n'
			<< "      --version                Prints the current version number, then exits." << '\n'
			<< "  -q, --quiet                  Show only minimal console output for getters; don't show any console output for setters." << '\n'
			<< "  -n, --no-color               Disables ANSI color sequences; this option is implied when -q|--quiet is specified." << '\n'
			<< "  -Q, --query                  Checks if the specified target is valid (without changing anything), then exits." << '\n'
			<< "  -l, --list                   Prints a list (sorted by ID) of all processes with an active audio session, then exits." << '\n'
			<< "  -v, --volume [0-100]         Gets or sets (when a number is specified) the volume of the target." << '\n'
			<< "  -I, --increment <0-100>      Increments the volume of the target by the specified number." << '\n'
			<< "  -D, --decrement <0-100>      Decrements the volume of the target by the specified number." << '\n'
			<< "  -m, --is-muted [true|false]  Gets or sets (when a boolean is specified) the mute state of the target." << '\n'
			<< "  -M, --mute                   Mutes the target.  (Equivalent to '-M=true')" << '\n'
			<< "  -U, --unmute                 Unmutes the target.  (Equivalent to '-M=false')" << '\n'
			<< "      --dev <i|o>              When [TARGET] is blank, selects the default input or output device; (Default: Output)" << '\n'
			<< "                                otherwise, limits device types to enumerate to the specified type. (Default: Both)" << '\n'
			;
	}
};

DEFINE_EXCEPTION(showhelp)

// Globals:
inline static bool quiet{ false };

enum class COLOR {
	WARN,
	ERR,
	HIGHLIGHT,
	ACCENT,
};
term::palette<COLOR> colors{
	std::make_pair(COLOR::WARN, color::yellow),
	std::make_pair(COLOR::ERR, color::orange),
	std::make_pair(COLOR::HIGHLIGHT, color::green),
	std::make_pair(COLOR::ACCENT, color::cyan),
};
inline constexpr size_t MARGIN_WIDTH{ 13ull };

// Forward Declarations:
inline std::string getTarget(const opt::ParamsAPI2&);
inline EDataFlow getDeviceDataFlow(const opt::ParamsAPI2&);
inline void handleVolumeArgs(const opt::ParamsAPI2&, const vccli::Volume*);
inline void handleMuteArgs(const opt::ParamsAPI2&, const vccli::Volume*);



int main(const int argc, char** argv)
{
	using namespace vccli;
	int rc{ 0 };
	try {
		using namespace opt_literals;
		opt::ParamsAPI2 args{ argc, argv,
			'v'_opt,
			"volume"_opt,
			'm'_opt,
			"is-muted"_opt,
			'I'_req,
			"increment"_req,
			'D'_req,
			"decrement"_req,
			"dev"_req
		};

		quiet = args.check_any<opt::Flag, opt::Option>('q', "quiet");
		colors.setActive(!quiet && !args.check_any<opt::Flag, opt::Option>('n', "no-color"));

		if (args.empty() || args.check_any<opt::Flag, opt::Option>('h', "help")) {
			std::cout << PrintHelp{};
			return 0;
		}
		else if (args.checkopt("version")) {
			if (!quiet) std::cout << "vccli v";
			std::cout << vccli_VERSION_EXTENDED << '\n';
			return 0;
		}

		const std::string target{ getTarget(args) };

		// Initialize Windows API
		if (const auto& hr{ CoInitializeEx(NULL, COINIT::COINIT_MULTITHREADED) }; hr != S_OK)
			throw make_exception("Failed to initialize COM interface with error code ", hr, "!");

		// Get controller:
		const auto& targetController{ AudioAPI::getObject(target, getDeviceDataFlow(args)) };

		if (args.check_any<opt::Flag, opt::Option>('Q', "query")) {
			const bool isValid{ targetController->isValid() };
			const auto& typeName{ targetController->type_name() };
			if (!quiet) {
				const std::string head{ isValid ? "Found" : "Not Found" };
				std::cout << head << ':' << indent(MARGIN_WIDTH, head.size() + 2ull) << colors(isValid ? COLOR::HIGHLIGHT : COLOR::ERR) << targetController->resolved_name << colors() << '\n';
				if (isValid)
					std::cout
					<< "Type:" << indent(MARGIN_WIDTH, 6ull) << colors(COLOR::HIGHLIGHT) << typeName.value_or("Undefined") << (target.empty() ? " (Default)" : "") << colors() << '\n'
					<< "Volume:" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::HIGHLIGHT) << str::stringify(std::fixed, std::setprecision(0), targetController->getVolumeScaled()) << colors() << '\n'
					<< "Muted:" << indent(MARGIN_WIDTH, 7ull) << colors(COLOR::HIGHLIGHT) << str::stringify(std::boolalpha, targetController->getMuted()) << colors() << '\n';
			}
			else std::cout << typeName.value_or("false") << '\n';
		}
		else if (args.check_any<opt::Flag, opt::Option>('l', "list")) {
			for (const auto& [pid, pname] : AudioAPI::GetAllAudioProcessesSorted(EDataFlow::eRender)) {
				std::string pid_s{ std::to_string(pid) };
				if (!quiet) std::cout << '[' << colors(COLOR::HIGHLIGHT);
				std::cout << pid_s;
				if (!quiet) std::cout << colors() << ']' << indent(MARGIN_WIDTH, pid_s.size() + 2ull);
				std::cout << ' ' << pname << '\n';
			}
		}
		else if (targetController->isValid()) {
			// Handle Volume Args:
			handleVolumeArgs(args, targetController.get());

			// Handle Mute Args:
			handleMuteArgs(args, targetController.get());
		}
		else throw make_exception("Couldn't Find Target:  '", target, "'!");

	} catch (const GET_EXCEPTION(showhelp)& ex) {
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
inline std::string getTarget(const opt::ParamsAPI2& args)
{
	auto params{ args.typegetv_all<opt::Parameter>() };
	if (params.empty()) return{};
	// Copy & pop the first parameter from the vector
	std::string target{ params.front() };
	params.erase(params.begin());
	// If there are any additional parameters, throw
	if (!params.empty()) throw make_custom_exception<GET_EXCEPTION(showhelp)>("Unexpected Arguments:  ", str::join(params, ", "));
	return target;
}

inline EDataFlow getDeviceDataFlow(const opt::ParamsAPI2& args)
{
	if (const auto& dev{ args.typegetv<opt::Option>("dev") }; dev.has_value()) {
		std::string v{ str::tolower(dev.value()) };
		if (str::equalsAny<true>(v, "i", "in", "input", "rec", "record", "recording"))
			return EDataFlow::eCapture;
		else if (str::equalsAny<true>(v, "o", "out", "output", "play", "playback"))
			return EDataFlow::eRender;
		else throw make_exception("Invalid Device Flow State:  ", v, " ; (expected i/in/input/rec/record/recording || o/out/output/play/playback)!");
	}
	else return EDataFlow::eAll;
}
inline void handleVolumeArgs(const opt::ParamsAPI2& args, const vccli::Volume* controller)
{
	const auto& increment{ args.typegetv_any<opt::Flag, opt::Option>('I', "increment") }, & decrement{ args.typegetv_any<opt::Flag, opt::Option>('D', "decrement") };
	if (increment.has_value() && decrement.has_value())
		throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-I", colors(), '|', colors(COLOR::ERR), "--increment", colors(), " && ", colors(COLOR::ERR), "-D", colors(), '|', colors(COLOR::ERR), "--decrement", colors());
	else if (increment.has_value()) {
		const auto& value{ increment.value() };
		if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
			throw make_exception("Invalid Number Specified:  ", value);
		if (controller->getVolumeScaled() == 100.0f)
			if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 10ull) << colors(COLOR::WARN) << "100" << colors() << '\n';
			else {
				controller->incrementVolume(str::stof(value) / 100.0f);
				if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::HIGHLIGHT) << static_cast<int>(controller->getVolumeScaled()) << colors() << " (+" << colors(COLOR::HIGHLIGHT) << value << colors() << ')' << '\n';
			}
	}
	else if (decrement.has_value()) {
		const auto& value{ decrement.value() };
		if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
			throw make_exception("Invalid Number Specified:  ", value);
		if (controller->getVolumeScaled() == 0.0f)
			if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 10ull) << colors(COLOR::WARN) << "0" << colors() << '\n';
			else {
				controller->decrementVolume(str::stof(value) / 100.0f);
				if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::HIGHLIGHT) << static_cast<int>(controller->getVolumeScaled()) << colors() << " (-" << colors(COLOR::HIGHLIGHT) << value << colors() << ')' << '\n';
			}
	}
	if (args.check_any<opt::Flag, opt::Option>('v', "volume")) {
		if (const auto& captured{ args.typegetv_any<opt::Flag, opt::Option>('v', "volume") }; captured.has_value()) {
			// Set
			const auto& value{ captured.value() };
			if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
				throw make_exception("Invalid Number Specified:  ", value);
			float tgtVolume{ str::stof(captured.value()) };
			if (tgtVolume > 100.0f)
				tgtVolume = 100.0f;
			else if (tgtVolume < 0.0f)
				tgtVolume = 0.0f;
			if (controller->getVolumeScaled() == tgtVolume)
				if (!quiet) std::cout << "Volume is" << indent(MARGIN_WIDTH, 10ull) << colors(COLOR::WARN) << static_cast<int>(tgtVolume) << colors() << '\n';
				else {
					controller->setVolumeScaled(tgtVolume);
					if (!quiet) std::cout << "Volume =" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::HIGHLIGHT) << static_cast<int>(tgtVolume) << colors() << '\n';
				}
		}
		else {
			// Get
			if (!quiet) std::cout << "Volume:" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::HIGHLIGHT);
			std::cout << str::stringify(std::fixed, std::setprecision(0), controller->getVolume() * 100.0f);
			if (!quiet) std::cout << colors() << '\n';
		}
	}
}
inline void handleMuteArgs(const opt::ParamsAPI2& args, const vccli::Volume* controller)
{
	const bool
		mute{ args.check_any<opt::Flag, opt::Option>('M', "mute") },
		unmute{ args.check_any<opt::Flag, opt::Option>('U', "unmute") };

	if (mute && unmute)
		throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " && ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors());
	else if (mute) {
		if (controller->getMuted() == true) {
			if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "true" << colors() << '\n';

		}
		else {
			controller->mute();
			if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::HIGHLIGHT) << "true" << colors() << '\n';
		}
	}
	else if (unmute) {
		if (controller->getMuted() == false) {
			if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "false" << colors() << '\n';
		}
		else {
			controller->unmute();
			if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::HIGHLIGHT) << "false" << colors() << '\n';
		}
	}

	if (args.check_any<opt::Flag, opt::Option>('m', "is-muted")) {
		if (const auto& captured{ args.typegetv_any<opt::Flag, opt::Option>('m', "is-muted") }; captured.has_value()) {
			if (mute || unmute) throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-M", colors(), '|', colors(COLOR::ERR), "--is-muted", colors(COLOR::ERR), " && (", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " || ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors(), ')');
			// Set
			const auto& value{ str::trim(captured.value()) };
			if (str::equalsAny<true>(value, "true", "1", "on")) {
				if (controller->getMuted() == true)
					if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "true" << colors() << '\n';
					else {
						controller->mute();
						if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::HIGHLIGHT) << "true" << colors() << '\n';
					}
			}
			else if (str::equalsAny<true>(value, "false", "0", "off")) {
				if (controller->getMuted() == false)
					if (!quiet) std::cout << "Muted is" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::WARN) << "false" << colors() << '\n';
					else {
						controller->unmute();
						if (!quiet) std::cout << "Muted =" << indent(MARGIN_WIDTH, 8ull) << colors(COLOR::HIGHLIGHT) << "false" << colors() << '\n';
					}
			}
			else throw make_exception("Invalid Argument Specified:  '", colors(COLOR::ERR), captured.value(), colors(), "';  Expected a boolean value ('", colors(COLOR::ERR), "true", colors(), "'/'", colors(COLOR::ERR), "false", colors(), "')!");
		}
		else {
			// Get
			if (!quiet) std::cout << "Is Muted:" << indent(MARGIN_WIDTH, 9ull) << colors(COLOR::HIGHLIGHT);
			std::cout << str::stringify(std::boolalpha, controller->getMuted());
			if (!quiet) std::cout << colors() << '\n';
		}
	}
}