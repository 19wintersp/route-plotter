#include <cmath>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

#include <EuroScopePlugIn.hpp>



#define PLUGIN_NAME    "Route plotter"
#define PLUGIN_VERSION "0.4.0"
#define PLUGIN_AUTHORS "Patrick Winters"
#define PLUGIN_LICENCE "GNU GPLv3"



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

public:
	Plugin(void);
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
