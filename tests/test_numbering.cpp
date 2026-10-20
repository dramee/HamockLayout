#include "hammock/hammocks.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

#include <random>

using hammock::computeNumbering;
using hammock::Graph;
using hammock::Numbering;
using test_helpers::makeGraph;

namespace {

std::vector<int> byName(const Graph& g, const std::vector<int>& numbers, std::initializer_list<const char*> names) {
    std::vector<int> out;
    for (const char* name : names) out.push_back(numbers[*g.find(name)]);
    return out;
}

bool isPermutation(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] != static_cast<int>(i) + 1) return false;
    }
    return true;
}

}  // namespace

TEST(numbering_of_chain) {
    const Graph g = makeGraph({"a>b", "b>c", "c>d"});
    const Numbering num = computeNumbering(g);
    const std::vector<int> expected{1, 2, 3, 4};
    CHECK(byName(g, num.M, {"a", "b", "c", "d"}) == expected);
    CHECK(byName(g, num.N, {"a", "b", "c", "d"}) == expected);
    CHECK(byName(g, num.L, {"a", "b", "c", "d"}) == expected);
}

TEST(numbering_of_if_then_else) {
    // Обход: entry, cond, then, join, exit (удалена первой), ... , else.
    const Graph g = makeGraph({"entry>cond", "cond>then", "cond>else", "then>join", "else>join", "join>exit"});
    const Numbering num = computeNumbering(g);
    const auto names = {"entry", "cond", "then", "else", "join", "exit"};
    CHECK((byName(g, num.M, names) == std::vector<int>{1, 2, 3, 6, 4, 5}));
    CHECK((byName(g, num.N, names) == std::vector<int>{1, 2, 4, 3, 5, 6}));
    CHECK((byName(g, num.L, names) == std::vector<int>{1, 2, 4, 3, 5, 6}));
}

TEST(numbering_of_loop_reorders_l) {
    // N: entry 1, h 2, exit 3, x 4, y 5, z 6. При перенумерации относительно h
    // множество S(h) = {h, x, y, z} ставится сразу за h, выход уходит в конец.
    const Graph g = makeGraph({"entry>h", "h>x", "x>y", "y>z", "z>h", "h>exit"});
    const Numbering num = computeNumbering(g);
    const auto names = {"entry", "h", "x", "y", "z", "exit"};
    CHECK((byName(g, num.M, names) == std::vector<int>{1, 2, 3, 4, 5, 6}));
    CHECK((byName(g, num.N, names) == std::vector<int>{1, 2, 4, 5, 6, 3}));
    CHECK((byName(g, num.L, names) == std::vector<int>{1, 2, 3, 4, 5, 6}));
    for (int k = 1; k <= g.vertexCount(); ++k) CHECK_EQ(num.L[num.byL[k - 1]], k);
}

TEST(numbering_of_nested_loops) {
    // entry > a > b > c > b, c > a, a > exit.
    // N: entry 1, a 2, exit 3, b 4, c 5. S(a) = {a, b, c}; S(b) = {b, c}.
    const Graph g = makeGraph({"entry>a", "a>b", "b>c", "c>b", "c>a", "a>exit"});
    const Numbering num = computeNumbering(g);
    const auto names = {"entry", "a", "b", "c", "exit"};
    CHECK((byName(g, num.N, names) == std::vector<int>{1, 2, 4, 5, 3}));
    CHECK((byName(g, num.L, names) == std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(numbering_requires_program_graph) {
    CHECK_THROWS(computeNumbering(makeGraph({"a>b", "c>b"})), hammock::GraphError);
}

TEST(numbering_properties_on_random_graphs) {
    std::mt19937 rng(12345);
    for (int iter = 0; iter < 300; ++iter) {
        const int n = 2 + iter % 40;
        const Graph g = test_helpers::randomProgramGraph(n, iter % 3 == 0 ? n * 2 : n / 2, rng);
        const Numbering num = computeNumbering(g);
        REQUIRE(isPermutation(num.M));
        REQUIRE(isPermutation(num.N));
        REQUIRE(isPermutation(num.L));
        CHECK_EQ(num.M[g.entry()], 1);
        CHECK_EQ(num.N[g.entry()], 1);
        CHECK_EQ(num.L[g.entry()], 1);
        for (int k = 1; k <= n; ++k) CHECK_EQ(num.L[num.byL[k - 1]], k);
        // N — обратный порядок завершения обхода: дуга u -> v с N[v] <= N[u]
        // возможна только в предка (обратная дуга), а у предка M-номер не больше.
        for (int u = 0; u < n; ++u) {
            for (const int v : g.successors(u)) {
                if (num.N[v] <= num.N[u]) CHECK(num.M[v] <= num.M[u]);
            }
        }
    }
}
