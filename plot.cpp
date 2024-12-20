#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>

#include <algorithm>
#include <format>
#include <memory>
#include <iterator>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>
#undef max //

#include <gdiplus.h>
#include <gdiplusgraphics.h>

#include <EuroScopePlugIn.hpp>



#define PLUGIN_NAME    "Route plotter"
#define PLUGIN_VERSION "0.4.1"
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
	bool highlight = false;
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

class Screen : public EuroScope::CRadarScreen {
private:
	size_t i;

public:
	Screen(size_t _i) : i(_i) {}

	void OnAsrContentToBeClosed(void) override;
	void OnRefresh(HDC, int) override;
};

class Plugin : public EuroScope::CPlugIn {
	friend class Screen;

private:
	std::unordered_map<std::string, Route> routes;
	std::unordered_map<std::string, std::unique_ptr<Source>> sources;
	std::vector<Screen *> screens;
	int name_counter = 0;

public:
	Plugin(void);

	bool OnCompileCommand(const char *) override;
	Screen *OnRadarScreenCreated(const char *, bool, bool, bool, bool) override;

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

class RouteSource : public virtual Source {
public:
	const char *HelpArguments() const override {
		return "<ROUTE>";
	}

	const char *HelpDescription() const override {
		return "Plot a flight plan route";
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
	plugin = nullptr;
}



void Screen::OnAsrContentToBeClosed() {
	if (plugin) plugin->screens[i] = nullptr;
	delete this;
}

const double HOLD_RADIUS = 2.0;
const double STROKE_WIDTH = 1.0;
const double LABEL_INTERVAL = 0.25;
const int FONT_SIZE = 12;

const double DEG_LAT_PER_NM = 60.007;
const double DEG_PER_RAD = 180.0 / std::numbers::pi;

static Gdiplus::Color colour(double t) {
	int x = 255.0 * (1.0 - std::abs(1.0 - t * 2.0));
	return Gdiplus::Color(t < 0.5 ? 255 : x, 0, t > 0.5 ? 255 : x);
}

void Screen::OnRefresh(HDC hdc, int phase) {
	using namespace Gdiplus;

	if (phase != EuroScope::REFRESH_PHASE_BACK_BITMAP) return;

	Graphics *ctx = Graphics::FromHDC(hdc);

	FontFamily font_family(L"EuroScope");
	Font font(&font_family, FONT_SIZE, FontStyleRegular, UnitPixel);

	RECT rect = GetRadarArea();
	Rect clip(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
	ctx->SetClip(clip);

	Pen pen(colour(0.0), STROKE_WIDTH), brush_pen(colour(0.0), STROKE_WIDTH);

	EuroScope::CPosition position1, position2;
	POINT point1, point2;

	struct Label {
		std::wstring content;
		double x, y, angle;
	};

	double dist = 0.0, inter = LABEL_INTERVAL * (double) (rect.bottom - rect.top);
	std::vector<Label> labels;

	for (const auto &[name, route] : plugin->routes) {
		size_t n = route.size() - 1;
		for (size_t i = 0; i <= n; i++) {
			if (route[i].IsDiscontinuity()) continue;

			position1 = std::move(position2);
			point1 = std::move(point2);

			position2.m_Latitude = route[i].lat;
			position2.m_Longitude = route[i].lon;

			point2 = ConvertCoordFromPositionToPixel(position2);

			if (auto hold = route[i].hold) {
				// approximation
				double crs_rad = hold->course / DEG_PER_RAD;
				double lat_rad = route[i].lat / DEG_PER_RAD;
				double len_deg = hold->length / DEG_LAT_PER_NM;
				position2.m_Latitude -= len_deg * std::cos(crs_rad);
				position2.m_Longitude -= len_deg * std::sin(crs_rad) / std::cos(lat_rad);

				POINT point_ie = point2, point_is = ConvertCoordFromPositionToPixel(position2);

				long leg_x = point_is.x - point_ie.x, leg_y = point_is.y - point_ie.y;
				double mul = HOLD_RADIUS / hold->length;
				long rad_x = (double) leg_y * mul, rad_y = (double) -leg_x * mul;

				long d = std::round(2.0 * std::sqrt(rad_x * rad_x + rad_y * rad_y)), r = d / 2;

				double ang = std::atan((double) rad_y / (double) rad_x) * DEG_PER_RAD;
				if (
					(rad_x <= rad_y && rad_x <= -rad_y) ||
					(rad_x < rad_y && rad_x > -rad_y && ang < 0) ||
					(rad_x > rad_y && rad_x < -rad_y && ang > 0)
				) ang += 180;

				if (hold->left_turns) {
					rad_x *= -1; rad_y *= -1;
				}

				POINT point_oc = point_ie, point_ic = point_is;

				point_oc.x += rad_x; point_oc.y += rad_y;
				point_ic.x += rad_x; point_ic.y += rad_y;

				POINT point_os = point_oc, point_oe = point_ic;

				point_os.x += rad_x; point_os.y += rad_y;
				point_oe.x += rad_x; point_oe.y += rad_y;

				pen.SetColor(colour((double) i / (double) n));

				ctx->DrawArc(&pen, point_oc.x - r, point_oc.y - r, d, d, ang, -180);
				ctx->DrawLine(&pen, point_os.x, point_os.y, point_oe.x, point_oe.y);
				ctx->DrawArc(&pen, point_ic.x - r, point_ic.y - r, d, d, ang, 180);
				ctx->DrawLine(&pen, point_is.x, point_is.y, point_ie.x, point_ie.y);
			}

			if (i < 1 || route[i - 1].IsDiscontinuity()) continue;

			auto line_brush = LinearGradientBrush(
				Point(point1.x, point1.y), Point(point2.x, point2.y),
				colour((double) (i - 1) / (double) n), colour((double) i / (double) n)
			);
			brush_pen.SetBrush(&line_brush);

			ctx->DrawLine(&brush_pen, point1.x, point1.y, point2.x, point2.y);

			if (!clip.Contains(point2.x, point2.y)) continue;

			double length = std::hypot(point2.x - point1.x, point2.y - point1.y);
			for (double target = inter; target < dist + length; target += inter) {
				double t = (target - dist) / length;
				labels.push_back({
					std::wstring(name.begin(), name.end()),
					(1.0 - t) * point1.x + t * point2.x,
					(1.0 - t) * point1.y + t * point2.y,
					std::atan((double) (point1.y - point2.y) / (double) (point2.x - point1.x))
				});
			}
			dist = std::fmod(dist + length, inter);
		}
	}

	SolidBrush brush(Color(0xdd, 0xdd, 0xdd));

	for (const auto &label : labels) {
		ctx->TranslateTransform(label.x, label.y);
		ctx->RotateTransform(-label.angle * DEG_PER_RAD);
		ctx->TranslateTransform(-label.x, -label.y);

		PointF centre(label.x, label.y);
		ctx->DrawString(label.content.c_str(), -1, &font, centre, &brush);

		ctx->ResetTransform();
	}

	brush.SetColor(Color(0xff, 0xff, 0xff));
	pen.SetColor(Color(0xff, 0xff, 0xff));

	std::vector<RectF> label_rects;
	RectF label_rect;

	for (const auto &[_, route] : plugin->routes) {
		size_t n = route.size() - 1;
		for (size_t i = 0; i <= n; i++) {
			if (route[i].IsDiscontinuity()) continue;

			position1.m_Latitude = route[i].lat;
			position1.m_Longitude = route[i].lon;

			point1 = ConvertCoordFromPositionToPixel(position1);

			int r = route[i].highlight ? 4 : 1;
			ctx->DrawEllipse(&pen, point1.x - r, point1.y - r, r * 2, r * 2);

			if (route[i].label.size() > 0) {
				PointF origin(point1.x + r + 4, point1.y - (FONT_SIZE / 2));

				ctx->MeasureString(route[i].label.c_str(), -1, &font, origin, &label_rect);
				if (std::none_of(
					label_rects.cbegin(), label_rects.cend(),
					[label_rect](auto &rect2) { return rect2.IntersectsWith(label_rect); }
				)) {
					ctx->DrawString(route[i].label.c_str(), -1, &font, origin, &brush);
					label_rects.push_back(label_rect);
				}
			}
		}
	}
}



Plugin::Plugin(void) :
	EuroScope::CPlugIn(
		EuroScope::COMPATIBILITY_CODE,
		PLUGIN_NAME, PLUGIN_VERSION, PLUGIN_AUTHORS, PLUGIN_LICENCE
	)
{
	sources["coords"] = std::make_unique<CoordsSource>();
	sources["route"] = std::make_unique<RouteSource>();
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

		display_command("[NAME] <ROUTE>", "Shortcut for \".plot route [NAME] <ROUTE>\"", width);
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

		for (auto screen : screens)
			if (screen) screen->RefreshMapContent();

		return true;
	}

	auto source = sources.find(parts[1]);
	int offset = 2;

	if (source == sources.cend()) {
		source = sources.find("route");
		offset = 1;
	}

	std::string name = std::to_string(++name_counter), error;
	Route route;

	std::string_view sv(command);
	size_t ofs = 0;
	for (int i = 0; i < offset; i++)
		ofs = sv.find_first_not_of(' ', sv.find_first_of(' ', ofs));

	if (source->second->Parse(
		parts.begin() + offset, parts.end(),
		command + ofs,
		route, name, error
	)) {
		if (route.size() > 0) {
			routes[name] = route;

			for (auto screen : screens)
				if (screen) screen->RefreshMapContent();
		}

		return true;
	} else {
		display_message("Error", error.c_str(), true);
		return false;
	}
}

Screen *Plugin::OnRadarScreenCreated(const char *, bool, bool, bool geo, bool) {
	if (!geo) return nullptr;

	auto *screen = new Screen(screens.size());
	screens.push_back(screen);

	return screen;
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



bool RouteSource::Parse(
	std::vector<std::string>::iterator start,
	std::vector<std::string>::iterator end,
	const char *,
	Route &route,
	std::string &name,
	std::string &error
) const {
	struct Point {
		std::string_view name, runway;
		struct {
			int len, crs;
			bool lh;
		} hold;
	};

	struct Position {
		double lat, lon;

		inline bool operator==(const Position &) const = default;
	};

	if ((end - start) % 2 == 0) name = *(start++);

	std::vector<Point> points;
	std::vector<std::string_view> ats_routes;

	std::unordered_map<std::string_view, Position> point_positions;
	std::unordered_map<std::string_view, std::vector<Position>> ats_route_positions;

	auto it = start;
	for (bool p = true; it != end; p ^= 1, it++) {
		if (p) {
			Point point;
			point.hold.len = 0;

			std::size_t sep = it->find('/');
			if (sep == std::string::npos) {
				point.name = *it;
			} else {
				point.name = std::string_view(it->data(), sep);

				sep++;
				if (it->size() - sep > 3) {
					try {
						int crs = std::stoi(std::string(*it, sep, 3));
						sep += 3;

						char dir = std::toupper((*it)[sep++]);
						if (dir != 'R' && dir != 'L') {
							error = std::string("invalid hold direction");
							return false;
						}

						int len = it->size() > sep ? std::stoi(std::string(*it, sep)) : 4;

						point.hold.crs = crs;
						point.hold.len = len;
						point.hold.lh = dir == 'L';
					} catch (const std::invalid_argument &_) {
						error = std::string("invalid integer");
						return false;
					}
				} else if (it == start || end - it == 1) {
					point.runway = std::string_view(it->data() + sep, it->size() - sep);
				} else {
					error = std::string("runway in nonterminal location");
					return false;
				}
			}

			Position position = { 0.0, 0.0 };

			if ('0' <= point.name[0] && point.name[0] <= '9') {
				const char *src = point.name.data();
				char *lat_sign, *lon_sign;

				auto int_lat = std::strtoul(src, &lat_sign, 10);
				if (!*lat_sign || (*lat_sign != 'N' && *lat_sign != 'S')) goto ll_fail;
				auto int_lon = std::strtoul(lat_sign + 1, &lon_sign, 10);
				if (!*lon_sign || (*lon_sign != 'E' && *lon_sign != 'W')) goto ll_fail;

				for (int i = (lat_sign - src) / 2; i > 1; i--) {
					position.lat += (double) (int_lat % 100);
					position.lat /= 60.0;
					int_lat /= 100;
				}

				for (int i = (lon_sign - lat_sign - 1) / 2; i > 1; i--) {
					position.lon += (double) (int_lon % 100);
					position.lon /= 60.0;
					int_lon /= 100;
				}

				position.lat += (double) int_lat;
				position.lon += (double) int_lon;

				position.lat *= *lat_sign == 'S' ? -1.0 : 1.0;
				position.lon *= *lon_sign == 'W' ? -1.0 : 1.0;

			ll_fail: {}
			} else {
				position.lat = NAN;
			}

			point_positions.emplace(point.name, position);
			points.push_back(std::move(point));
		} else if (*it == "DCT") {
			ats_routes.push_back({});
		} else {
			ats_route_positions.emplace(*it, std::vector<Position>());
			ats_routes.push_back(*it);
		}
	}

	std::vector<Position> sid, star;
	Position adep, ades;

	EuroScope::CPosition pos;

	for (
		auto el = plugin->SectorFileElementSelectFirst(EuroScope::SECTOR_ELEMENT_ALL);
		el.IsValid();
		el = plugin->SectorFileElementSelectNext(el, EuroScope::SECTOR_ELEMENT_ALL)
	) {
		switch (el.GetElementType()) {
			case EuroScope::SECTOR_ELEMENT_AIRPORT:
				for (int i = 0; i <= 1; i++) {
					const Point &point = i ? points.front() : points.back();
					Position &adp = i ? adep : ades;

					if (point.runway.empty() && !point.name.compare(el.GetName()))
						if (el.GetPosition(&pos, 0))
							adp = { pos.m_Latitude, pos.m_Longitude };
				}

			case EuroScope::SECTOR_ELEMENT_VOR:
			case EuroScope::SECTOR_ELEMENT_NDB:
			case EuroScope::SECTOR_ELEMENT_FIX: {
				auto it = point_positions.find(el.GetName());
				if (it != point_positions.end())
					if (el.GetPosition(&pos, 0))
						std::get<1>(*it) = { pos.m_Latitude, pos.m_Longitude };

				break;
			}

			case EuroScope::SECTOR_ELEMENT_RUNWAY:
				for (int i = 0; i <= 1; i++) {
					const Point &point = i ? points.front() : points.back();
					Position &adp = i ? adep : ades;

					if (point.runway.data() && !point.name.compare(0, 4, el.GetAirportName(), 4))
						for (int j = 0; j <= 1; j++)
							if (!point.runway.compare(el.GetRunwayName(j)))
								if (el.GetPosition(&pos, j))
									adp = { pos.m_Latitude, pos.m_Longitude };
				}

				break;

			// this will break if an aerodrome has an identically-named SID & STAR lol
			case EuroScope::SECTOR_ELEMENT_SIDS_STARS:
				if (ats_routes.empty()) break;

				for (int i = 0; i <= 1; i++) {
					const Point &point = i ? points.front() : points.back();
					const std::string_view &ats = i ? ats_routes.front() : ats_routes.back();
					std::vector<Position> &out = i ? sid : star;

					if (
						out.empty() &&
						!point.name.compare(el.GetAirportName()) &&
						(point.runway.empty() || !point.runway.compare(el.GetRunwayName(0))) &&
						!ats.compare(el.GetName())
					)
						for (int j = 0; el.GetPosition(&pos, j); j++)
							out.push_back({ pos.m_Latitude, pos.m_Longitude });
				}

				break;

			case EuroScope::SECTOR_ELEMENT_LOW_AIRWAY:
			case EuroScope::SECTOR_ELEMENT_HIGH_AIRWAY: {
				auto it = ats_route_positions.find(el.GetName());
				if (it != ats_route_positions.end())
					for (int i = 0; el.GetPosition(&pos, i); i++) {
						auto &vec = std::get<1>(*it);
						Position npos = { pos.m_Latitude, pos.m_Longitude };
						if (vec.empty() || npos != vec.back()) vec.push_back(npos);
					}

				break;
			}
		}
	}

	if (!star.empty()) star.push_back(ades);

	Position seg_end, seg_start;

	for (int i = 0; i < points.size(); i++) {
		if (i == 0 && !sid.empty()) {
			seg_end = adep;
		} else if (i == points.size() - 1 && !star.empty()) {
			seg_end = ades;
		} else {
			auto it = point_positions.find(points[i].name);
			if (it == point_positions.end() || std::isnan(it->second.lat)) {
				error = std::format("could not find point '{}'", points[i].name);
				return false;
			} else {
				seg_end = std::get<1>(*it);
			}
		}

		if (i > 0 && ats_routes[i - 1].data()) {
			const std::vector<Position> *ats;

			if (i == 1 && !sid.empty()) {
				ats = &sid;
			} else if (i == points.size() - 1 && !star.empty()) {
				ats = &star;
			} else {
				auto it = ats_route_positions.find(ats_routes[i - 1]);
				if (it == ats_route_positions.end()) {
					error = std::format("could not find airway '{}'", ats_routes[i - 1]);
					return false;
				} else {
					ats = &std::get<1>(*it);
				}
			}

			auto from = ats->cbegin(), to = ats->cend();

			if (ats != &sid) {
				if ((from = std::find(ats->cbegin(), ats->cend(), seg_start)) == ats->cend()) {
					error = std::format(
						"discontinuity ({} to {})",
						points[i - 1].name,
						ats_routes[i - 1]
					);

					return false;
				}
			}

			if (ats != &star) {
				if ((to = std::find(ats->cbegin(), ats->cend(), seg_end)) == ats->cend()) {
					error = std::format(
						"discontinuity ({} to {})",
						ats_routes[i - 1],
						points[i].name
					);

					return false;
				}
			}

			if (from < to)
				for (auto it = (ats == &sid || ats == &star) ? from : ++from; it < to; it++)
					route.push_back({ it->lat, it->lon });
			else
				for (auto it = --from; it > to; it--)
					route.push_back({ it->lat, it->lon });
		}

		Node point(seg_end.lat, seg_end.lon);

		if (points[i].hold.len) {
			point.hold = Hold(
				points[i].hold.len,
				points[i].hold.crs,
				points[i].hold.lh
			);
		} else {
			point.hold = std::nullopt;
		}

		if (points[i].name[0] < '0' || points[i].name[0] > '9') {
			std::wstring label;
			for (int j = 0; j < points[i].name.size(); j++)
				label.push_back((std::wstring::value_type) points[i].name[j]);
			point.label = label;
		}

		route.push_back(point);

		seg_start = seg_end;
	}

	return true;
}
