#include "hammock/graphml.hpp"
#include "hammock/layout.hpp"
#include "hammock/svg.hpp"
#include "hammock/xml.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <map>
#include <random>
#include <set>

using hammock::Graph;
using hammock::layout::Drawing;
using hammock::layout::layoutGraph;
using hammock::layout::LayoutOptions;
using hammock::layout::Point;
using test_helpers::makeGraph;

namespace {

const std::string kExamplesDir = HAMMOCK_EXAMPLES_DIR;
constexpr double kTol = 1e-6;

bool same(const Point& a, const Point& b) { return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6; }

struct Rect {
    double x, y, w, h;
};

bool inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x - kTol && inner.y >= outer.y - kTol && inner.x + inner.w <= outer.x + outer.w + kTol &&
           inner.y + inner.h <= outer.y + outer.h + kTol;
}

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w - kTol && b.x < a.x + a.w - kTol && a.y < b.y + b.h - kTol && b.y < a.y + a.h - kTol;
}

bool onTop(const Point& p, const Rect& r) {
    return std::abs(p.y - r.y) < 1e-6 && p.x >= r.x - kTol && p.x <= r.x + r.w + kTol;
}

bool onBottom(const Point& p, const Rect& r) {
    return std::abs(p.y - (r.y + r.h)) < 1e-6 && p.x >= r.x - kTol && p.x <= r.x + r.w + kTol;
}

// Проверяет геометрию рисунка и то, что каждая дуга графа нарисована
// непрерывной цепочкой маршрутов от источника до приёмника.
void checkDrawing(const Graph& g, const Drawing& d) {
    const int n = g.vertexCount();
    REQUIRE(static_cast<int>(d.vertices.size()) == n);
    const Rect page{0, 0, d.width, d.height};
    std::vector<Rect> boxes;
    for (const auto& v : d.vertices) {
        boxes.push_back({v.x, v.y, v.width, v.height});
        CHECK(v.width > 0 && v.height > 0);
        CHECK(inside(boxes.back(), page));
    }
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) CHECK(!overlap(boxes[a], boxes[b]));
    }

    // Рамки гамаков: вершина внутри рамки тогда и только тогда, когда она в гамаке.
    CHECK_EQ(d.clusters.size(), d.tree.clusters.size() - 1);
    std::map<int, Rect> clusterRect;
    for (const auto& c : d.clusters) {
        const Rect r{c.x, c.y, c.width, c.height};
        clusterRect[c.cluster] = r;
        CHECK(inside(r, page));
        CHECK(onTop(c.entryPort, r));
        CHECK(onBottom(c.exitPort, r));
        for (int v = 0; v < n; ++v) {
            if (d.tree.contains(c.cluster, v)) {
                CHECK(inside(boxes[v], r));
            } else {
                CHECK(!overlap(boxes[v], r));
            }
        }
    }
    for (const auto& c : d.clusters) {
        const int parent = d.tree.clusters[c.cluster].parent;
        if (parent != hammock::HammockTree::kRoot) CHECK(inside(clusterRect[c.cluster], clusterRect[parent]));
        for (const auto& other : d.clusters) {
            if (other.cluster == c.cluster) continue;
            const bool nested = d.tree.clusters[other.cluster].firstL >= d.tree.clusters[c.cluster].firstL &&
                                d.tree.clusters[other.cluster].lastL <= d.tree.clusters[c.cluster].lastL;
            const bool nestedBack = d.tree.clusters[c.cluster].firstL >= d.tree.clusters[other.cluster].firstL &&
                                    d.tree.clusters[c.cluster].lastL <= d.tree.clusters[other.cluster].lastL;
            if (!nested && !nestedBack) CHECK(!overlap(clusterRect[c.cluster], clusterRect[other.cluster]));
        }
    }

    // Маршруты по каждой исходной дуге.
    std::map<std::pair<int, int>, std::vector<const hammock::layout::EdgeRoute*>> byEdge;
    for (const auto& route : d.edges) {
        REQUIRE(route.points.size() >= 2);
        for (const auto& p : route.points) CHECK(p.x >= -kTol && p.x <= d.width + kTol && p.y >= -kTol && p.y <= d.height + kTol);
        for (const auto& e : route.represents) byEdge[e].push_back(&route);
    }
    for (int u = 0; u < n; ++u) {
        for (const int v : g.successors(u)) {
            const auto it = byEdge.find({u, v});
            REQUIRE(it != byEdge.end());
            const auto& routes = it->second;
            int finals = 0;
            const hammock::layout::EdgeRoute* first = nullptr;
            for (const auto* r : routes) {
                if (r->arrow) {
                    ++finals;
                    if (u == v) {
                        CHECK(r->selfLoop);
                    } else {
                        CHECK(onTop(r->points.back(), boxes[v]));  // стрелка упирается в верх приёмника
                    }
                } else {
                    // Маршрут кончается в выходном порту гамака, откуда начинается следующий.
                    bool continued = false;
                    for (const auto* next : routes) continued = continued || same(next->points.front(), r->points.back());
                    CHECK(continued);
                }
                if (u == v || onBottom(r->points.front(), boxes[u])) first = r;
            }
            CHECK_EQ(finals, 1);
            CHECK(first != nullptr);
        }
    }
}

void checkGraph(const Graph& g) {
    checkDrawing(g, layoutGraph(g));
    LayoutOptions flat;
    flat.useHammocks = false;
    const Drawing d = layoutGraph(g, flat);
    CHECK(d.clusters.empty());
    CHECK_EQ(d.edges.size(), static_cast<std::size_t>(g.edgeCount()));  // без гамаков дуга = маршрут
    checkDrawing(g, d);
}

}  // namespace

TEST(layout_if_then_else_draws_hammock_box) {
    const Graph g = makeGraph({"entry>cond", "cond>then", "cond>else", "then>join", "else>join", "join>exit"});
    const Drawing d = layoutGraph(g);
    checkDrawing(g, d);
    REQUIRE(d.clusters.size() == 1);
    const auto& box = d.clusters[0];
    CHECK_EQ(g.id(box.start), std::string("cond"));
    CHECK_EQ(g.id(box.end), std::string("join"));
    // Входной порт рамки — над начальной вершиной, дуга из гамака — одна (в join).
    const auto& cond = d.vertices[*g.find("cond")];
    CHECK(std::abs(box.entryPort.x - (cond.x + cond.width / 2)) < 1e-6);
    int fromBox = 0;
    for (const auto& e : d.edges) {
        if (same(e.points.front(), box.exitPort)) ++fromBox;
    }
    CHECK_EQ(fromBox, 1);
    // Вершины идут сверху вниз: entry, cond, then/else, join, exit.
    auto y = [&](const char* name) { return d.vertices[*g.find(name)].y; };
    CHECK(y("entry") < y("cond"));
    CHECK(y("cond") < y("then"));
    CHECK(std::abs(y("then") - y("else")) < 1e-6);
    CHECK(y("then") < y("join"));
    CHECK(y("join") < y("exit"));
}

TEST(layout_merges_exit_edges_of_hammock) {
    // Из гамака {c, a, b} в j ведут три дуги, но снаружи рамки рисуется одна.
    const Graph g = makeGraph({"s>c", "c>a", "c>b", "c>j", "a>j", "b>j", "j>t"});
    const Drawing d = layoutGraph(g);
    checkDrawing(g, d);
    REQUIRE(d.clusters.size() == 1);
    for (const auto& e : d.edges) {
        if (e.level == hammock::HammockTree::kRoot && e.represents.size() > 1) CHECK_EQ(e.represents.size(), static_cast<std::size_t>(3));
    }
}

TEST(layout_nested_loops) {
    const Graph g = makeGraph({"entry>i0", "i0>ic", "ic>j0", "j0>jc", "jc>cmp", "cmp>swap", "swap>jinc", "cmp>jinc",
                               "jinc>jc", "jc>iinc", "iinc>ic", "ic>ret", "ret>exit"});
    const Drawing d = layoutGraph(g);
    checkDrawing(g, d);
    CHECK_EQ(d.clusters.size(), static_cast<std::size_t>(3));
    int backEdges = 0;
    for (const auto& e : d.edges) backEdges += e.reversed ? 1 : 0;
    CHECK_EQ(backEdges, 2);  // jinc -> jc и iinc -> ic
    // Внешние рамки идут раньше внутренних.
    for (std::size_t i = 1; i < d.clusters.size(); ++i) CHECK(d.clusters[i - 1].depth <= d.clusters[i].depth);
}

TEST(layout_self_loop_and_chain) {
    checkGraph(makeGraph({"s>a", "a>a", "a>b", "b>t"}));
    checkGraph(makeGraph({"s>t"}));
}

TEST(layout_examples) {
    const char* names[] = {"01_if_then_else", "02_while_loop", "03_euclid_gcd", "04_bubble_sort",
                           "05_binary_search", "06_switch", "07_loop_with_break", "08_irreducible",
                           "09_sequence", "10_nested_if", "11_repeat_in_while", "12_random_structured"};
    for (const char* name : names) {
        const Graph g = hammock::loadGraphML(kExamplesDir + "/" + name + ".graphml");
        checkGraph(g);
    }
}

TEST(layout_random_graphs) {
    std::mt19937 rng(42);
    test_helpers::StructuredBuilder builder(rng);
    for (int iter = 0; iter < 150; ++iter) {
        const Graph g = iter % 2 ? builder.build(3 + iter % 40)
                                 : test_helpers::randomProgramGraph(2 + iter % 25, iter % 15, rng);
        checkGraph(g);
    }
}

TEST(layout_is_deterministic) {
    std::mt19937 rng(3);
    test_helpers::StructuredBuilder builder(rng);
    const Graph g = builder.build(40);
    CHECK_EQ(hammock::layout::renderSvg(g, layoutGraph(g)), hammock::layout::renderSvg(g, layoutGraph(g)));
}

TEST(layout_rejects_non_program_graphs) {
    CHECK_THROWS(layoutGraph(makeGraph({"a>b", "c>b"})), hammock::GraphError);
}

TEST(display_length_counts_code_points) {
    CHECK_EQ(hammock::layout::displayLength("abc"), static_cast<std::size_t>(3));
    CHECK_EQ(hammock::layout::displayLength("вход"), static_cast<std::size_t>(4));
    // Ширина вершины растёт с длиной подписи.
    const Graph g = makeGraph({"s>very long label here", "very long label here>t"});
    const Drawing d = layoutGraph(g);
    CHECK(d.vertices[*g.find("very long label here")].width > d.vertices[*g.find("s")].width);
}

TEST(svg_is_well_formed_and_complete) {
    const Graph g = makeGraph({"entry>a < b ?", "a < b ?>x & y", "a < b ?>\"q\"", "x & y>join", "\"q\">join", "join>exit"});
    const Drawing d = layoutGraph(g);
    hammock::layout::SvgOptions options;
    options.title = "Тест <svg>";
    const std::string svg = hammock::layout::renderSvg(g, d, options);
    const auto root = hammock::xml::parse(svg);  // корректный XML
    CHECK_EQ(root->localName, std::string("svg"));
    CHECK_EQ(root->ns, std::string("http://www.w3.org/2000/svg"));

    int vertexGroups = 0, hammockGroups = 0, paths = 0;
    std::set<std::string> labels;
    std::vector<const hammock::xml::Element*> stack{root.get()};
    while (!stack.empty()) {
        const auto* e = stack.back();
        stack.pop_back();
        const std::string* cls = e->attribute("class");
        if (cls && *cls == "vertex") ++vertexGroups;
        if (cls && *cls == "hammock") ++hammockGroups;
        if (e->localName == "path" && e->attribute("stroke")) ++paths;
        if (e->localName == "text") labels.insert(e->text);
        for (const auto& child : e->children) stack.push_back(child.get());
    }
    CHECK_EQ(vertexGroups, g.vertexCount());
    CHECK_EQ(hammockGroups, static_cast<int>(d.clusters.size()));
    CHECK_EQ(paths, static_cast<int>(d.edges.size()));
    CHECK(labels.count("a < b ?") == 1);
    CHECK(labels.count("x & y") == 1);
    CHECK(labels.count("\"q\"") == 1);
    CHECK(labels.count("Тест <svg>") == 1);
}
