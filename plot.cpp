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

class CoordsSource : public virtual Source {
public:
	const char *HelpArguments() const override {
		return "<STRING>";
	}

	const char *HelpDescription() const override {
		return "Plot a string of coordinates, encoded in the legacy format";
	}

	bool Parse(
		std::vector<std::string>::iterator, std::vector<std::string>::iterator,
		const char *, Route &, std::string &, std::string &
	) const override;
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
{
	sources["coords"] = std::make_unique<CoordsSource>();
}

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



static signed char decode(char c) {
	if ('A' <= c && c <= 'Z') return c - 'A';
	if ('a' <= c && c <= 'z') return 26 + c - 'a';
	if ('0' <= c && c <= '9') return 52 + c - '0';
	return -1;
}

bool CoordsSource::Parse(
	std::vector<std::string>::iterator args_it,
	std::vector<std::string>::iterator args_end,
	const char *command,
	Route &route,
	std::string &name,
	std::string &error
) const {
	if (args_it >= args_end || !*command) {
		error = std::string("missing string");
		return false;
	}

	if (args_end - args_it > 1 && args_it->find('(') == std::string::npos) {
		name = *args_it;

		command += args_it->size();
		command += std::strspn(command, " ");
	}

	Node item;
	signed char word[7];

	if (command[0] != '@') command--;

	while (*(++command)) {
		if (*command == '(' && route.size()) {
			int count = 1;
			const char *start = command + 1, *end;

			while (*(++command)) {
				end = strpbrk(command, "()");
				if (!end) {
					error = std::string("missing closing bracket");
					return false;
				}
				if (*end == '(') count++;
				else count--;
				command = end;
				if (!count) break;
			}

			std::wstring sc(end - start + 1, L'\0');
			for (const char *c = start; c < end; c++) sc[c - start] = *c;
			route.back().label = std::wstring(sc);

			continue;
		} else if (*command == '-') {
			item.lat = NAN;
			item.highlight = false;
		} else if ((word[0] = decode(*command)) >= 0) {
			for (int i = 1; i < 7; i++)
				if ((word[i] = decode(*(++command))) < 0) {
					error = std::string("invalid character");
					return false;
				}

			item.lat = word[1] + ((double) word[2] + (double) word[3] / 60) / 60;
			item.lon = word[4] + ((double) word[5] + (double) word[6] / 60) / 60;

			item.lat += 60.0 * ((word[0] >> 2) & 0b01);
			item.lon += 60.0 * ((word[0] >> 4) & 0b11);

			if (word[0] & 0b0010) item.lat = -item.lat;
			if (word[0] & 0b1000) item.lon = -item.lon;

			item.highlight = false;
			item.hold = std::nullopt;

			if (word[0] & 1) {
				signed char extra1 = decode(*(++command));
				if (extra1 < 0) {
					error = std::string("invalid character");
					return false;
				}

				if (extra1 >= 60) {
					item.highlight = true;
				} else {
					signed char extra2 = decode(*(++command));
					if (extra2 < 0) {
						error = std::string("invalid character");
						return false;
					}

					item.hold = Hold(
						(double) (extra2 & 0b1111),
						6.0 * (double) extra1 + ((extra2 >> 5) ? 3.0 : 0.0),
						((extra2 >> 4) & 1) == 1
					);
				}
			}
		} else {
			error = std::string("invalid structural character");
			return false;
		}

		item.label.clear();
		route.push_back(item);
	}

	return true;
}
