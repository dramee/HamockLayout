#include "hammock/svg.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <sstream>

namespace hammock::layout {

namespace {

struct Palette {
    const char* fill;
    const char* stroke;
    const char* text;
};

// Цвета рамок гамаков по глубине вложенности.
constexpr Palette kClusterColors[] = {
    {"#e8f1fb", "#7aa7d6", "#3d6c9e"},
    {"#eaf6ec", "#7dbb8a", "#3f7d4c"},
    {"#fdf3e3", "#e0b25e", "#94681c"},
    {"#f3ecf9", "#b39ad6", "#6a4f94"},
    {"#fbeaea", "#d98c8c", "#9b3f3f"},
};

constexpr const char* kEdgeColor = "#555555";
constexpr const char* kBackEdgeColor = "#c0392b";

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", std::abs(v) < 0.05 ? 0.0 : v);
    return buf;
}

std::string escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

// Ломаная со скруглёнными изломами.
std::string pathData(const std::vector<Point>& raw, double radius) {
    std::vector<Point> pts;
    for (const auto& p : raw) {
        if (pts.empty() || std::hypot(p.x - pts.back().x, p.y - pts.back().y) > 0.01) pts.push_back(p);
    }
    if (pts.empty()) return {};
    std::string d = "M" + num(pts[0].x) + "," + num(pts[0].y);
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        const Point& a = pts[i - 1];
        const Point& b = pts[i];
        const Point& c = pts[i + 1];
        const double l1 = std::hypot(b.x - a.x, b.y - a.y);
        const double l2 = std::hypot(c.x - b.x, c.y - b.y);
        const double r = std::min({radius, l1 / 2, l2 / 2});
        const Point p1{b.x - (b.x - a.x) / l1 * r, b.y - (b.y - a.y) / l1 * r};
        const Point p2{b.x + (c.x - b.x) / l2 * r, b.y + (c.y - b.y) / l2 * r};
        d += " L" + num(p1.x) + "," + num(p1.y) + " Q" + num(b.x) + "," + num(b.y) + " " + num(p2.x) + "," +
             num(p2.y);
    }
    d += " L" + num(pts.back().x) + "," + num(pts.back().y);
    return d;
}

}  // namespace

std::string renderSvg(const Graph& g, const Drawing& drawing, const SvgOptions& options) {
    const double titleHeight = options.title.empty() ? 0 : 30;
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(drawing.width) << "\" height=\""
        << num(drawing.height + titleHeight) << "\" viewBox=\"0 0 " << num(drawing.width) << " "
        << num(drawing.height + titleHeight) << "\" font-family=\"Segoe UI, Helvetica, Arial, sans-serif\">\n"
        << "  <defs>\n";
    for (const auto& [id, color] : {std::make_pair("arrow", kEdgeColor), std::make_pair("arrow-back", kBackEdgeColor)}) {
        out << "    <marker id=\"" << id << "\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\" "
            << "markerHeight=\"7\" orient=\"auto-start-reverse\"><path d=\"M0,0 L10,5 L0,10 z\" fill=\"" << color
            << "\"/></marker>\n";
    }
    out << "  </defs>\n"
        << "  <rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    if (!options.title.empty()) {
        out << "  <text x=\"" << num(drawing.width / 2) << "\" y=\"20\" text-anchor=\"middle\" font-size=\"15\" "
            << "font-weight=\"600\" fill=\"#222\">" << escape(options.title) << "</text>\n";
    }
    out << "  <g transform=\"translate(0," << num(titleHeight) << ")\">\n";

    // Рамки гамаков: внешние раньше внутренних.
    out << "    <g class=\"hammocks\">\n";
    for (const auto& box : drawing.clusters) {
        const Palette& color = kClusterColors[(box.depth - 1) % std::size(kClusterColors)];
        out << "      <g class=\"hammock\"><title>гамак " << escape(g.id(box.start)) << " → " << escape(g.id(box.end))
            << "</title>\n"
            << "        <rect x=\"" << num(box.x) << "\" y=\"" << num(box.y) << "\" width=\"" << num(box.width)
            << "\" height=\"" << num(box.height) << "\" rx=\"8\" fill=\"" << color.fill << "\" stroke=\""
            << color.stroke << "\" stroke-width=\"1.2\" stroke-dasharray=\"5,3\"/>\n";
        if (options.showClusterLabels) {
            const bool portOnRight = box.entryPort.x - box.x > box.width / 2;
            const double tx = portOnRight ? box.x + 7 : box.x + box.width - 7;
            out << "        <text x=\"" << num(tx) << "\" y=\"" << num(box.y + 13) << "\" text-anchor=\""
                << (portOnRight ? "start" : "end") << "\" font-size=\"11\" fill=\"" << color.text << "\">"
                << escape(g.id(box.start)) << " → " << escape(g.id(box.end)) << "</text>\n";
        }
        out << "        <circle cx=\"" << num(box.exitPort.x) << "\" cy=\"" << num(box.exitPort.y)
            << "\" r=\"2.5\" fill=\"" << color.stroke << "\"/>\n"
            << "      </g>\n";
    }
    out << "    </g>\n";

    out << "    <g class=\"edges\" fill=\"none\" stroke-width=\"1.4\">\n";
    for (const auto& e : drawing.edges) {
        const bool back = e.reversed || e.selfLoop;
        out << "      <path d=\"" << pathData(e.points, options.cornerRadius) << "\" stroke=\""
            << (back ? kBackEdgeColor : kEdgeColor) << "\"";
        if (e.arrow) out << " marker-end=\"url(#" << (back ? "arrow-back" : "arrow") << ")\"";
        out << "/>\n";
    }
    out << "    </g>\n";

    out << "    <g class=\"vertices\" font-size=\"13\">\n";
    for (int v = 0; v < g.vertexCount(); ++v) {
        const VertexBox& b = drawing.vertices[v];
        const char* fill = "#ffffff";
        const char* stroke = "#444444";
        if (v == g.entry()) {
            fill = "#d9f2dd";
            stroke = "#2e7d32";
        } else if (v == g.exit()) {
            fill = "#fde0dc";
            stroke = "#c62828";
        }
        out << "      <g class=\"vertex\"><rect x=\"" << num(b.x) << "\" y=\"" << num(b.y) << "\" width=\""
            << num(b.width) << "\" height=\"" << num(b.height) << "\" rx=\"" << num(b.height / 2.5) << "\" fill=\""
            << fill << "\" stroke=\"" << stroke << "\" stroke-width=\"1.4\"/>"
            << "<text x=\"" << num(b.x + b.width / 2) << "\" y=\"" << num(b.y + b.height / 2)
            << "\" text-anchor=\"middle\" dominant-baseline=\"central\" fill=\"#222\">" << escape(g.id(v))
            << "</text></g>\n";
    }
    out << "    </g>\n  </g>\n</svg>\n";
    return out.str();
}

}  // namespace hammock::layout
