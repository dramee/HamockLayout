#include "hammock/sugiyama.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <random>
#include <set>

using namespace hammock::sugiyama;

namespace {

Input makeInput(int n, std::initializer_list<std::pair<int, int>> edges, double width = 40, double height = 30) {
    Input in;
    in.nodes.assign(n, NodeSpec{width, height, -1, -1});
    for (const auto& [a, b] : edges) in.edges.push_back({a, b, 1});
    return in;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

// Общие свойства любой укладки.
void checkLayout(const Input& in, const Result& r, const Options& opt = {}) {
    const int n = static_cast<int>(in.nodes.size());
    REQUIRE(static_cast<int>(r.position.size()) == n);
    REQUIRE(r.routes.size() == in.edges.size());
    // Всё внутри [0, width] × [0, height].
    for (int v = 0; v < n; ++v) {
        CHECK(r.position[v].x >= -1e-6 && r.position[v].x + in.nodes[v].width <= r.width + 1e-6);
        CHECK(r.position[v].y >= -1e-6 && r.position[v].y + in.nodes[v].height <= r.height + 1e-6);
    }
    for (const auto& route : r.routes) {
        for (const auto& p : route) CHECK(p.x >= -1e-6 && p.x <= r.width + 1e-6 && p.y >= -1e-6 && p.y <= r.height + 1e-6);
    }
    // Вершины одного слоя не перекрываются и идут в порядке order.
    for (int a = 0; a < n; ++a) {
        for (int b = 0; b < n; ++b) {
            if (a == b || r.layer[a] != r.layer[b] || r.order[a] >= r.order[b]) continue;
            CHECK(r.position[a].x + in.nodes[a].width + opt.dummyGap - 1e-6 <= r.position[b].x);
        }
    }
    // Слои идут сверху вниз и не перекрываются по вертикали.
    for (int a = 0; a < n; ++a) {
        for (int b = 0; b < n; ++b) {
            if (r.layer[a] < r.layer[b]) CHECK(r.position[a].y + in.nodes[a].height < r.position[b].y);
        }
    }
    for (std::size_t e = 0; e < in.edges.size(); ++e) {
        const auto& edge = in.edges[e];
        const auto& route = r.routes[e];
        REQUIRE(route.size() >= 2);
        if (edge.from == edge.to) continue;
        // После обращения обратных дуг каждая дуга идёт строго вниз по слоям.
        const int upper = r.reversed[e] ? edge.to : edge.from;
        const int lower = r.reversed[e] ? edge.from : edge.to;
        CHECK(r.layer[upper] < r.layer[lower]);
        // Начало — на нижней стороне источника, конец — на верхней стороне приёмника.
        const auto& from = r.position[edge.from];
        const auto& to = r.position[edge.to];
        CHECK(near(route.front().y, from.y + in.nodes[edge.from].height));
        CHECK(route.front().x >= from.x - 1e-6 && route.front().x <= from.x + in.nodes[edge.from].width + 1e-6);
        CHECK(near(route.back().y, to.y));
        CHECK(route.back().x >= to.x - 1e-6 && route.back().x <= to.x + in.nodes[edge.to].width + 1e-6);
        if (!r.reversed[e]) {
            for (std::size_t i = 1; i < route.size(); ++i) CHECK(route[i].y >= route[i - 1].y - 1e-6);
        }
    }
}

}  // namespace

TEST(sugiyama_chain_is_vertical) {
    Input in = makeInput(4, {{0, 1}, {1, 2}, {2, 3}});
    in.source = 0;
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK_EQ(r.layerCount, 4);
    CHECK_EQ(r.dummyCount, 0);
    CHECK_EQ(r.crossings, 0);
    for (int v = 0; v < 4; ++v) {
        CHECK_EQ(r.layer[v], v);
        CHECK(near(r.position[v].x, r.position[0].x));
    }
}

TEST(sugiyama_diamond_is_symmetric) {
    Input in = makeInput(4, {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK_EQ(r.crossings, 0);
    CHECK_EQ(r.layer[3], 2);
    // Источник и сток — посередине между ветвями.
    const double mid = (r.position[1].x + r.position[2].x) / 2;
    CHECK(near(r.position[0].x, mid));
    CHECK(near(r.position[3].x, mid));
}

TEST(sugiyama_long_edges_get_dummies) {
    Input in = makeInput(4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK_EQ(r.dummyCount, 2);
    CHECK_EQ(r.routes[3].size(), static_cast<std::size_t>(2 + 2 * 2));  // по две точки на фиктивную вершину
}

TEST(sugiyama_breaks_cycles_by_reversing_back_edges) {
    Input in = makeInput(4, {{0, 1}, {1, 2}, {2, 1}, {2, 3}, {3, 0}});
    in.source = 0;
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK(!r.reversed[0]);
    CHECK(!r.reversed[1]);
    CHECK(r.reversed[2]);  // 2 -> 1
    CHECK(!r.reversed[3]);
    CHECK(r.reversed[4]);  // 3 -> 0
    CHECK_EQ(r.layer[0], 0);
}

TEST(sugiyama_source_and_sink_constraints) {
    // Без ограничений вершина 2 (её единственный предок — 0) могла бы оказаться в слое 1 вместе с 1.
    Input in = makeInput(4, {{0, 1}, {1, 3}, {0, 2}, {2, 3}, {0, 3}});
    in.source = 0;
    in.sink = 2;
    in.edges = {{0, 1, 1}, {1, 3, 1}, {0, 2, 1}, {3, 2, 1}};
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK_EQ(r.layer[0], 0);
    for (int v = 0; v < 4; ++v) {
        if (v != 2) CHECK(r.layer[v] < r.layer[2]);
    }
    CHECK_EQ(r.layer[2], r.layerCount - 1);
}

TEST(sugiyama_minimizes_crossings) {
    // Порядок обхода в глубину даёт слой 2 = (x, y) и пересечение b-x с a-y;
    // упорядочивание должно его убрать.
    const int s = 0, a = 1, b = 2, x = 3, y = 4, t = 5;
    Input in = makeInput(6, {{s, a}, {s, b}, {a, x}, {a, y}, {b, x}, {x, t}, {y, t}});
    in.source = s;
    in.sink = t;
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK_EQ(r.crossings, 0);
}

TEST(sugiyama_respects_ports) {
    Input in = makeInput(3, {{0, 1}, {1, 2}});
    in.nodes[1] = {100, 60, 10, 90};  // широкая вершина: вход слева, выход справа
    in.source = 0;
    const Result r = layout(in);
    checkLayout(in, r);
    CHECK(near(r.routes[0].back().x, r.position[1].x + 10));
    CHECK(near(r.routes[1].front().x, r.position[1].x + 90));
    // Соседи выровнены по портам: дуги вертикальны.
    CHECK(near(r.routes[0].front().x, r.routes[0].back().x));
    CHECK(near(r.routes[1].front().x, r.routes[1].back().x));
}

TEST(sugiyama_self_loops_and_parallel_edges) {
    Input in = makeInput(3, {{0, 1}, {1, 1}, {1, 2}, {1, 2}});
    const Result r = layout(in);
    checkLayout(in, r);
    REQUIRE(r.routes[1].size() == 4);
    const double right = r.position[1].x + in.nodes[1].width;
    CHECK(near(r.routes[1].front().x, right));
    CHECK(near(r.routes[1].back().x, right));
    CHECK(r.routes[1][1].x > right);
}

TEST(sugiyama_spreads_ports_of_merging_edges) {
    Input in = makeInput(5, {{0, 1}, {0, 2}, {0, 3}, {1, 4}, {2, 4}, {3, 4}}, 80);
    const Result r = layout(in);
    checkLayout(in, r);
    std::set<long> ends;
    for (int e = 3; e < 6; ++e) ends.insert(std::lround(r.routes[e].back().x * 10));
    CHECK_EQ(ends.size(), static_cast<std::size_t>(3));
}

TEST(sugiyama_handles_degenerate_inputs) {
    CHECK_EQ(layout(Input{}).position.size(), static_cast<std::size_t>(0));
    const Result single = layout(makeInput(1, {}));
    CHECK_EQ(single.layerCount, 1);
    CHECK(near(single.width, 40));
    Input bad = makeInput(2, {{0, 5}});
    CHECK_THROWS(layout(bad), std::invalid_argument);
}

TEST(sugiyama_random_graphs_satisfy_invariants) {
    std::mt19937 rng(8);
    for (int iter = 0; iter < 200; ++iter) {
        const int n = 2 + iter % 25;
        Input in;
        for (int v = 0; v < n; ++v) {
            in.nodes.push_back({20.0 + static_cast<double>(rng() % 80), 20.0 + static_cast<double>(rng() % 40), -1, -1});
        }
        const int m = static_cast<int>(rng() % (3 * n));
        for (int e = 0; e < m; ++e) in.edges.push_back({static_cast<int>(rng() % n), static_cast<int>(rng() % n), 1});
        in.source = iter % 3 == 0 ? -1 : 0;
        const Result r = layout(in);
        checkLayout(in, r);
    }
}
