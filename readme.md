# route-plotter

**A EuroScope plugin for drawing routes and coordinate strings.**

This simple plugin allows drawing arbitrary "routes" (strings of geographic
points) on the EuroScope radar screen.

Licensed under the GNU General Public License, version 3.

## User guide

After installing the plugin to a suitable directory, load it, and ensure that
permission is given to draw on all display types.

The plugin is controlled via the command line. Run `.plot help` to show the list
of available commands. Each plot added to the screen is either given a name
explcitly in the command, or receives a sequential numbered name. The name is
written on the screen in grey alongside the route.

"Route" strings are encoded as follows:

```abnf
route = [dep] *(point SP (airway / "DCT") SP) point [arr]
dep   = airport ["/" runway] SP (sid / "DCT") SP
arr   = SP (star / "DCT") SP airport ["/" runway]
point = (fix / vor / ndb / airport / coord) ["/" hold]
coord = 2*DIGIT ("N" / "S") 3*DIGIT ("E" / "W")
hold  = 3DIGIT ("L" / "R") [1*DIGIT] ; length given in nmi
```

"Coords" strings are encoded using a legacy format designed to be generated by
external tools.

Please open an Issue for any bug reports or feature requests.

## Build instructions

The build system is set up for a \*nix environment with the Clang C++ tooling
and [`xwin`](https://github.com/Jake-Shadle/xwin/), though should work with any
C++20 compiler targeting Windows. To build, run `make`; the plugin is written to
"bin/plot.dll".
