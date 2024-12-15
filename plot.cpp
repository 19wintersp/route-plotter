#include <cmath>
#include <cstring>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>
#undef max //

#include <EuroScopePlugIn.hpp>



#define PLUGIN_NAME    "Route plotter"
#define PLUGIN_VERSION "0.4.0"
#define PLUGIN_AUTHORS "Patrick Winters"
#define PLUGIN_LICENCE "GNU GPLv3"
#define PLUGIN_WEBSITE "https://github.com/19wintersp/route-plotter"
#define COMMAND_PREFIX ".plot"



namespace EuroScope = EuroScopePlugIn;

struct Hold {
	double length, course;
	bool left_turns;

	Hold(double len, double crs, bool lh) : length(len), course(crs), left_turns(lh) {}
};

struct Node {
	double lat, lon;
	bool highlight;
	std::wstring label;
	std::optional<Hold> hold;

	Node() = default;
	Node(double _lat, double _lon) : lat(_lat), lon(_lon) {}

	static Node Discontinuity() {
		return Node(NAN, NAN);
	}

	bool IsDiscontinuity() const {
		return std::isnan(lat) || std::isnan(lon);
	}
};

using Route = std::vector<Node>;

class Source {
public:
	virtual const char *HelpArguments() const {
		return "";
	}

	virtual const char *HelpDescription() const {
		return "null source";
	}

	virtual bool Parse(
		std::vector<std::string>::iterator args_it,
		std::vector<std::string>::iterator args_end,
		const char *args_src,
		Route &route,
		std::string &name,
		std::string &error
	) const {
		error = std::string("not implemented");
		return false;
	}
};

class Plugin : public EuroScope::CPlugIn {
private:
	std::unordered_map<std::string, Route> routes;
	std::unordered_map<std::string, std::unique_ptr<Source>> sources;
	int name_counter = 0;

public:
	Plugin(void);

	bool OnCompileCommand(const char *) override;

private:
	void display_message(const char *from, const char *msg, bool urgent = false);
	void display_command(const char *command, const char *help, size_t width);
};



Plugin *plugin;

void __declspec(dllexport) EuroScopePlugInInit(EuroScope::CPlugIn **ptr) {
	*ptr = plugin = new Plugin;
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
	delete plugin;
}



Plugin::Plugin(void) :
	EuroScope::CPlugIn(
		EuroScope::COMPATIBILITY_CODE,
		PLUGIN_NAME, PLUGIN_VERSION, PLUGIN_AUTHORS, PLUGIN_LICENCE
	)
{}

bool Plugin::OnCompileCommand(const char *command) {
	std::istringstream buf(command);
	std::vector<std::string> parts(std::istream_iterator<std::string>(buf), {});

	if (parts.empty() || parts[0] != COMMAND_PREFIX) return false;

	if (parts.size() == 1 || parts[1] == "help") {
		size_t width = 15; // strlen("clear [NAME]...")
		for (const auto &[name, source] : sources) {
			size_t unique_size = name.size() + std::strlen(source->HelpArguments());
			width = std::max(width, unique_size + 8 /* strlen(" [NAME] ") */);
		}

		display_message("", "Available commands:");
		display_command("help", "Display this help text", width);
		display_command("clear [NAME]...", "Remove the named plot, or all plots", width);

		for (const auto &[name, source] : sources) {
			display_command(
				std::format("{} [NAME] {}", name, source->HelpArguments()).c_str(),
				source->HelpDescription(),
				width
			);
		}

		display_message("", "See <" PLUGIN_WEBSITE "> for more information.");

		return true;
	}

	if (parts[1] == "clear") {
		if (parts.size() > 2) {
			for (auto it = parts.cbegin() + 2; it < parts.cend(); it++) {
				routes.erase(*it);
			}
		} else {
			routes.clear();
		}

		return true;
	}

	auto source = sources.find(parts[1]);
	if (source != sources.cend()) {
		std::string name = std::to_string(++name_counter), error;
		Route route;

		std::string_view sv(command);
		size_t ofs = 0;
		for (int i = 0; i < 2; i++)
			ofs = sv.find_first_not_of(' ', sv.find_first_of(' ', ofs));

		if (source->second->Parse(
			parts.begin() + 2, parts.end(),
			command + ofs,
			route, name, error
		)) {
			routes[name] = route;
			return true;
		} else {
			display_message("Error", error.c_str(), true);
			return false;
		}
	}

	return false;
}

void Plugin::display_message(const char *from, const char *msg, bool urgent) {
	DisplayUserMessage(PLUGIN_NAME, from, msg, true, true, urgent, urgent, false);
}

void Plugin::display_command(const char *command, const char *help, size_t width) {
	display_message(
		"",
		std::format("  " COMMAND_PREFIX " {:<{}} - {}", command, width, help).c_str()
	);
}
