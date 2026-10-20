#include "hammock/hammock_tree.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <random>
#include <set>

using hammock::buildHammockTree;
using hammock::Graph;
using hammock::HammockTree;
using test_helpers::makeGraph;

namespace {

std::set<int> asSet(const std::vector<int>& v) { return {v.begin(), v.end()}; }

// Описание кластеров: "p>q" в порядке хранения (без корня).
std::string clusters(const Graph& g, const HammockTree& tree) {
    std::vector<std::string> items;
    for (std::size_t i = 1; i < tree.clusters.size(); ++i) {
        items.push_back(g.id(tree.clusters[i].start) + ">" + g.id(tree.clusters[i].end));
    }
    std::sort(items.begin(), items.end());
    std::string s;
    for (const auto& item : items) s += (s.empty() ? "" : " ") + item;
    return s;
}

// Проверяет структурные инварианты дерева гамаков.
void checkTreeInvariants(const Graph& g, const HammockTree& tree) {
    const int n = g.vertexCount();
    REQUIRE(!tree.clusters.empty());
    CHECK_EQ(tree.clusters[0].parent, -1);
    CHECK_EQ(tree.clusters[0].size(), n);
    std::vector<int> owner(n, -1);
    for (std::size_t c = 0; c < tree.clusters.size(); ++c) {
        const auto& cl = tree.clusters[c];
        const auto members = tree.members(static_cast<int>(c));
        if (c > 0) {
            // Каждый кластер — гамак, и вложен в родителя.
            CHECK(hammock::isHammock(g, members, cl.start, cl.end));
            CHECK(cl.size() >= 2);
            CHECK(!(cl.start == g.entry() && cl.end == g.exit()));
            const auto& parent = tree.clusters[cl.parent];
            CHECK(parent.firstL <= cl.firstL && cl.lastL <= parent.lastL);
            CHECK_EQ(cl.depth, parent.depth + 1);
            CHECK(std::find(parent.children.begin(), parent.children.end(), static_cast<int>(c)) !=
                  parent.children.end());
        }
        for (const int v : cl.ownVertices) {
            CHECK_EQ(owner[v], -1);
            owner[v] = static_cast<int>(c);
            CHECK_EQ(tree.innermost[v], static_cast<int>(c));
            for (const int child : cl.children) CHECK(!tree.contains(child, v));
        }
        // Ламинарность: любые два кластера вложены или не пересекаются.
        for (std::size_t d = c + 1; d < tree.clusters.size(); ++d) {
            const auto a = asSet(members);
            const auto b = asSet(tree.members(static_cast<int>(d)));
            std::vector<int> common;
            std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
            CHECK(common.empty() || common.size() == a.size() || common.size() == b.size());
        }
    }
    for (int v = 0; v < n; ++v) CHECK(owner[v] >= 0);

    // Порядок укладки: от меньших к большим, потомки раньше предков, корень последним.
    const auto order = tree.layoutOrder();
    REQUIRE(order.size() == tree.clusters.size());
    CHECK_EQ(order.back(), HammockTree::kRoot);
    std::vector<int> rank(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) rank[order[i]] = static_cast<int>(i);
    for (std::size_t i = 1; i + 1 < order.size(); ++i) {
        CHECK(tree.clusters[order[i - 1]].size() <= tree.clusters[order[i]].size());
    }
    for (std::size_t c = 1; c < tree.clusters.size(); ++c) CHECK(rank[c] < rank[tree.clusters[c].parent]);
}

}  // namespace

TEST(tree_of_if_then_else) {
    const Graph g = makeGraph({"entry>cond", "cond>then", "cond>else", "then>join", "else>join", "join>exit"});
    const HammockTree tree = buildHammockTree(g);
    CHECK_EQ(clusters(g, tree), std::string("cond>join"));
    REQUIRE(tree.clusters.size() == 2);
    CHECK((asSet(tree.members(1)) == std::set<int>{*g.find("cond"), *g.find("then"), *g.find("else")}));
    CHECK((asSet(tree.clusters[0].ownVertices) == std::set<int>{*g.find("entry"), *g.find("join"), *g.find("exit")}));
    checkTreeInvariants(g, tree);
}

TEST(tree_of_chain_is_flat) {
    // В цепочке любые два соседних гамака {a, b} и {b, c} пересекаются.
    const Graph g = makeGraph({"a>b", "b>c", "c>d", "d>e"});
    const HammockTree tree = buildHammockTree(g);
    CHECK_EQ(tree.clusters.size(), static_cast<std::size_t>(1));
    checkTreeInvariants(g, tree);
}

TEST(tree_of_nested_loops_and_branches) {
    // Пузырьковая сортировка: if внутри внутреннего цикла внутри внешнего.
    const Graph g = makeGraph({"entry>i0", "i0>ic", "ic>j0", "j0>jc", "jc>cmp", "cmp>swap", "swap>jinc", "cmp>jinc",
                               "jinc>jc", "jc>iinc", "iinc>ic", "ic>ret", "ret>exit"});
    const HammockTree tree = buildHammockTree(g);
    CHECK_EQ(clusters(g, tree), std::string("cmp>jinc ic>ret jc>iinc"));
    checkTreeInvariants(g, tree);
    // Глубины: ic>ret — 1, jc>iinc — 2, cmp>jinc — 3.
    for (std::size_t c = 1; c < tree.clusters.size(); ++c) {
        const std::string start = g.id(tree.clusters[c].start);
        const int expected = start == "ic" ? 1 : start == "jc" ? 2 : 3;
        CHECK_EQ(tree.clusters[c].depth, expected);
    }
}

TEST(tree_options) {
    const Graph g = makeGraph({"entry>cond", "cond>then", "cond>else", "then>join", "else>join", "join>exit"});
    hammock::HammockTreeOptions flat;
    flat.useHammocks = false;
    CHECK_EQ(buildHammockTree(g, flat).clusters.size(), static_cast<std::size_t>(1));

    hammock::HammockTreeOptions whole;
    whole.includeWholeProgram = true;
    const HammockTree tree = buildHammockTree(g, whole);
    CHECK_EQ(clusters(g, tree), std::string("cond>join entry>exit"));
    for (std::size_t c = 1; c < tree.clusters.size(); ++c) {
        // Ветвление вложено в гамак «вся программа».
        CHECK_EQ(tree.clusters[c].depth, tree.clusters[c].start == *g.find("cond") ? 2 : 1);
    }

    hammock::HammockTreeOptions big;
    big.minSize = 4;
    CHECK_EQ(buildHammockTree(g, big).clusters.size(), static_cast<std::size_t>(1));
}

TEST(crossing_flags_match_brute_force) {
    std::mt19937 rng(17);
    test_helpers::StructuredBuilder builder(rng);
    for (int iter = 0; iter < 300; ++iter) {
        const Graph g = iter % 2 ? builder.build(3 + iter % 30)
                                 : test_helpers::randomProgramGraph(3 + iter % 20, iter % 12, rng);
        const auto result = hammock::findHammocks(g);
        const auto flags = hammock::findCrossingHammocks(result);
        std::vector<std::set<int>> sets;
        for (const auto& span : result.spans) sets.push_back(asSet(result.members(span)));
        for (std::size_t i = 0; i < sets.size(); ++i) {
            bool crosses = false;
            for (std::size_t j = 0; j < sets.size() && !crosses; ++j) {
                if (i == j) continue;
                std::vector<int> common;
                std::set_intersection(sets[i].begin(), sets[i].end(), sets[j].begin(), sets[j].end(),
                                      std::back_inserter(common));
                crosses = !common.empty() && common.size() != sets[i].size() && common.size() != sets[j].size();
            }
            REQUIRE(flags[i] == crosses);
        }
    }
}

TEST(tree_invariants_on_random_graphs) {
    std::mt19937 rng(23);
    test_helpers::StructuredBuilder builder(rng);
    for (int iter = 0; iter < 200; ++iter) {
        const Graph g = iter % 2 ? builder.build(3 + iter % 50)
                                 : test_helpers::randomProgramGraph(3 + iter % 30, iter % 15, rng);
        checkTreeInvariants(g, buildHammockTree(g));
    }
}
