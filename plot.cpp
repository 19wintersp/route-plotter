#include <windows.h>

#include <EuroScopePlugIn.hpp>



#define PLUGIN_NAME    "Route plotter"
#define PLUGIN_VERSION "0.4.0"
#define PLUGIN_AUTHORS "Patrick Winters"
#define PLUGIN_LICENCE "GNU GPLv3"



namespace EuroScope = EuroScopePlugIn;

class Plugin : public EuroScope::CPlugIn {
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
