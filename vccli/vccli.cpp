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
			<< "  vccli <PID|PNAME>... [OPTIONS]" << '\n'
			<< '\n'
			<< "  Target application(s) can be specified using either their ProcessID (PID) or their ProcessName (PNAME)." << '\n'
			<< '\n'
			<< "OPTIONS:\n"
			<< "  -h, --help                   Shows this help display, then exits." << '\n'
			<< "      --version                Prints the current version number, then exits." << '\n'
			<< "  -q, --quiet                  Show only minimal console output for getters; don't show any console output for setters." << '\n'
			<< "  -n, --no-color               Disables the usage of ANSI color escape sequences in console output." << '\n'
			<< "  -v, --volume [0-100]         Gets or sets (when a number is specified) the volume of the target application." << '\n'
			<< "  -I, --increment <0-100>      Increments the volume of the target application by the specified number." << '\n'
			<< "  -D, --decrement <0-100>      Decrements the volume of the target application by the specified number." << '\n'
			<< "  -m, --is-muted [true|false]  Gets or sets (when a boolean is specified) the mute state of the target application." << '\n'
			<< "  -M, --mute                   Mutes the target application.  (Equivalent to '-M=true')" << '\n'
			<< "  -U, --unmute                 Unmutes the target application.  (Equivalent to '-M=false')" << '\n'
			;
	}
};

DEFINE_EXCEPTION(showhelp)

// Globals:
inline static bool quiet{ false };

enum class COLOR {
	ERR,
	HIGHLIGHT,
};
term::palette<COLOR> colors{
	std::make_pair(COLOR::ERR, color::yellow),
	std::make_pair(COLOR::HIGHLIGHT, color::green),
};

// Forward Declarations:
inline std::string getTarget(const opt::ParamsAPI2&);
inline void handleVolumeArgs(const opt::ParamsAPI2&, const vccli::ApplicationVolume&);
inline void handleMuteArgs(const opt::ParamsAPI2&, const vccli::ApplicationVolume&);



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
			"decrement"_req
		};

		quiet = args.check_any<opt::Flag, opt::Option>('q', "quiet");
		colors.setActive(!args.check_any<opt::Flag, opt::Option>('n', "no-color"));

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
		const auto& targetController{ AudioAPI::getApplicationVolumeObject(target) };

		// Handle Volume Args:
		handleVolumeArgs(args, targetController);

		// Handle Mute Args:
		handleMuteArgs(args, targetController);

	} catch (const GET_EXCEPTION(showhelp)& ex) {
		std::cerr << PrintHelp{} << '\n' << colors.get_fatal() << ex.what() << std::endl;
		rc = 1;
	} catch (const std::exception& ex) {
		std::cerr << colors.get_fatal() << ex.what() << std::endl;
		rc = 1;
	} catch (...) {
		std::cerr << colors.get_fatal() << "An undefined exception occurred!" << std::endl;
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
	if (params.empty()) throw make_custom_exception<GET_EXCEPTION(showhelp)>("No Target was Specified!  (Missing '", colors(COLOR::ERR), "<PID|PNAME>", colors(), "' ; See '", colors(COLOR::ERR), "USAGE", colors(), "')");
	// Copy & pop the first parameter from the vector
	std::string target{ params.front() };
	params.erase(params.begin());
	// If there are any additional parameters, throw
	if (!params.empty()) throw make_custom_exception<GET_EXCEPTION(showhelp)>("Unexpected Arguments:  ", str::join(params, ", "));
	return target;
}
inline void handleVolumeArgs(const opt::ParamsAPI2& args, const vccli::ApplicationVolume& controller)
{
	const auto& increment{ args.typegetv_any<opt::Flag, opt::Option>('I', "increment") }, & decrement{ args.typegetv_any<opt::Flag, opt::Option>('D', "decrement") };
	if (increment.has_value() && decrement.has_value())
		throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-I", colors(), '|', colors(COLOR::ERR), "--increment", colors(), " && ", colors(COLOR::ERR), "-D", colors(), '|', colors(COLOR::ERR), "--decrement", colors());
	else if (increment.has_value()) {
		const auto& value{ increment.value() };
		if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
			throw make_exception("Invalid Number Specified:  ", value);
		float volume{ (controller.getVolume() * 100.0f) + str::stof(value) };
		if (volume > 100.0f)
			volume = 100.0f;
		controller.setVolume(volume / 100.0f);
		if (!quiet) std::cout << "Volume = " << colors(COLOR::HIGHLIGHT) << static_cast<int>(volume) << colors() << " (+" << colors(COLOR::HIGHLIGHT) << value << colors() << ')' << std::endl;
	}
	else if (decrement.has_value()) {
		const auto& value{ decrement.value() };
		if (!std::all_of(value.begin(), value.end(), str::stdpred::isdigit))
			throw make_exception("Invalid Number Specified:  ", value);
		float volume{ (controller.getVolume() * 100.0f) - str::stof(value) };
		if (volume < 0.0f)
			volume = 0.0f;
		controller.setVolume(volume / 100.0f);
		if (!quiet) std::cout << "Volume = " << colors(COLOR::HIGHLIGHT) << static_cast<int>(volume) << colors() << " (-" << colors(COLOR::HIGHLIGHT) << value << colors() << ')' << std::endl;
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
			controller.setVolume(tgtVolume / 100.0f);
			if (!quiet) std::cout << "Volume = " << colors(COLOR::HIGHLIGHT) << static_cast<int>(tgtVolume) << colors() << std::endl;
		}
		else {
			// Get
			if (!quiet) std::cout << "Volume: " << colors(COLOR::HIGHLIGHT);
			std::cout << str::stringify(std::fixed, std::setprecision(0), controller.getVolume() * 100.0f);
			if (!quiet) std::cout << colors() << std::endl;
		}
	}
}
inline void handleMuteArgs(const opt::ParamsAPI2& args, const vccli::ApplicationVolume& controller)
{
	const bool
		mute{ args.check_any<opt::Flag, opt::Option>('M', "mute") },
		unmute{ args.check_any<opt::Flag, opt::Option>('U', "unmute") };

	if (mute && unmute)
		throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " && ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors());
	else if (mute) {
		controller.setMuted(true);
		if (!quiet) std::cout << "Muted = " << colors(COLOR::HIGHLIGHT) << "true" << colors() << std::endl;
	}
	else if (unmute) {
		controller.setMuted(false);
		if (!quiet) std::cout << "Muted = " << colors(COLOR::HIGHLIGHT) << "false" << colors() << std::endl;
	}

	if (args.check_any<opt::Flag, opt::Option>('m', "is-muted")) {
		if (const auto& captured{ args.typegetv_any<opt::Flag, opt::Option>('m', "is-muted") }; captured.has_value()) {
			if (mute || unmute) throw make_exception("Conflicting Options Specified:  ", colors(COLOR::ERR), "-M", colors(), '|', colors(COLOR::ERR), "--is-muted", colors(COLOR::ERR), " && (", colors(COLOR::ERR), "-m", colors(), '|', colors(COLOR::ERR), "--mute", colors(), " || ", colors(COLOR::ERR), "-u", colors(), '|', colors(COLOR::ERR), "--unmute", colors(), ')');
			// Set
			const auto& value{ str::trim(captured.value()) };
			if (str::equalsAny<true>(value, "true", "1", "on")) {
				controller.setMuted(true);
				if (!quiet) std::cout << "Muted = " << colors(COLOR::HIGHLIGHT) << "true" << colors() << std::endl;
			}
			else if (str::equalsAny<true>(value, "false", "0", "off")) {
				controller.setMuted(false);
				if (!quiet) std::cout << "Muted = " << colors(COLOR::HIGHLIGHT) << "false" << colors() << std::endl;
			}
			else throw make_exception("Invalid Argument Specified:  '", colors(COLOR::ERR), captured.value(), colors(), "';  Expected a boolean value ('", colors(COLOR::ERR), "true", colors(), "'/'", colors(COLOR::ERR), "false", colors(), "')!");
		}
		else {
			// Get
			if (!quiet) std::cout << "Is Muted: " << colors(COLOR::HIGHLIGHT);
			std::cout << str::stringify(std::boolalpha, controller.getMuted());
			if (!quiet) std::cout << colors() << std::endl;
		}
	}
}