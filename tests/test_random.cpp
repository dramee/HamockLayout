// Рандомизированные проверки: алгоритм Касьянова сравнивается с эталонами.
#include "hammock/hammocks.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

#include <iostream>
#include <random>

using hammock::Graph;
using hammock::Hammock;
using test_helpers::describe;
using test_helpers::kasyanov;
using test_helpers::reference;

namespace {

// Полный перебор по определению: все подмножества U и все p ∈ U.
// Применяются уточнения из hammocks.hpp:
//  * U не содержит выход графа;
//  * если U содержит вход, то p — вход. Формально определение допускает и
//    другую начальную вершину (у входа нет предшественников, поэтому условие
//    Г^-1(U \ {p}) ⊆ U для него выполняется автоматически), но по лемме 4
//    (Mp <= Mp' для всех p' ∈ U) началом такого гамака считается вход.
std::vector<Hammock> bruteForce(const Graph& g) {
    const int n = g.vertexCount();
    std::vector<Hammock> out;
    for (unsigned mask = 1; mask < (1u << n); ++mask) {
        if (mask & (1u << g.exit())) continue;
        std::vector<int> U;
        for (int v = 0; v < n; ++v) {
            if (mask & (1u << v)) U.push_back(v);
        }
        int q = -1;
        bool single = true;
        for (const int u : U) {
            for (const int w : g.successors(u)) {
                if (mask & (1u << w)) continue;
                if (q >= 0 && q != w) single = false;
                q = w;
            }
        }
        if (q < 0 || !single) continue;
        const bool containsEntry = (mask & (1u << g.entry())) != 0;
        for (const int p : U) {
            if (containsEntry && p != g.entry()) continue;
            if (hammock::isHammock(g, U, p, q)) out.push_back({p, q, U});
        }
    }
    return hammock::canonical(std::move(out));
}

bool reportMismatch(const Graph& g, const std::string& what, const std::string& actual, const std::string& expected) {
    if (actual == expected) return false;
    std::cerr << "  mismatch (" << what << ") on graph:\n";
    for (int v = 0; v < g.vertexCount(); ++v) {
        for (const int w : g.successors(v)) std::cerr << "    " << g.id(v) << " -> " << g.id(w) << "\n";
    }
    std::cerr << "    entry " << g.id(g.entry()) << ", exit " << g.id(g.exit()) << "\n"
              << "    actual:   " << actual << "\n    expected: " << expected << "\n";
    return true;
}

}  // namespace

TEST(random_small_graphs_match_exhaustive_search) {
    std::mt19937 rng(2024);
    for (int iter = 0; iter < 3000; ++iter) {
        const int n = 2 + iter % 9;  // до 10 вершин: 2^10 подмножеств
        const int extra = std::uniform_int_distribution<int>(0, 2 * n)(rng);
        const Graph g = test_helpers::randomProgramGraph(n, extra, rng);
        const std::string expected = describe(g, bruteForce(g));
        const bool bad = reportMismatch(g, "kasyanov vs brute force", kasyanov(g), expected) ||
                         reportMismatch(g, "reference vs brute force", reference(g), expected);
        REQUIRE(!bad);
    }
}

TEST(random_small_structured_graphs_match_exhaustive_search) {
    std::mt19937 rng(77);
    test_helpers::StructuredBuilder builder(rng);
    int checked = 0;
    for (int iter = 0; iter < 2000 && checked < 800; ++iter) {
        const Graph g = builder.build(1 + iter % 10);
        if (g.vertexCount() > 14) continue;
        ++checked;
        const std::string expected = describe(g, bruteForce(g));
        REQUIRE(!reportMismatch(g, "kasyanov vs brute force", kasyanov(g), expected));
    }
    CHECK(checked > 100);
}

TEST(random_medium_graphs_match_reference) {
    std::mt19937 rng(99);
    for (int iter = 0; iter < 400; ++iter) {
        const int n = 10 + iter % 50;
        const int extra = std::uniform_int_distribution<int>(0, n)(rng);
        const Graph g = test_helpers::randomProgramGraph(n, extra, rng);
        REQUIRE(!reportMismatch(g, "kasyanov vs reference", kasyanov(g), reference(g)));
    }
}

TEST(random_structured_graphs_match_reference) {
    std::mt19937 rng(31337);
    test_helpers::StructuredBuilder builder(rng);
    for (int iter = 0; iter < 300; ++iter) {
        const Graph g = builder.build(5 + iter % 60);
        REQUIRE(!reportMismatch(g, "kasyanov vs reference", kasyanov(g), reference(g)));
    }
}

TEST(result_does_not_depend_on_vertex_and_edge_order) {
    std::mt19937 rng(5);
    test_helpers::StructuredBuilder builder(rng);
    for (int iter = 0; iter < 200; ++iter) {
        const Graph g = iter % 2 ? builder.build(5 + iter % 40)
                                 : test_helpers::randomProgramGraph(3 + iter % 30, iter % 20, rng);
        const std::string expected = kasyanov(g);
        for (int k = 0; k < 3; ++k) {
            const Graph h = test_helpers::shuffled(g, rng);
            REQUIRE(!reportMismatch(h, "shuffled", kasyanov(h), expected));
        }
    }
}

TEST(lemma4_reference_hammocks_are_l_intervals) {
    // Лемма 4: вершины любого гамака имеют L-номера Lp, ..., Lp + |U| - 1,
    // а у конца q либо Mq < Mp, либо Lq = Lp + |U|.
    std::mt19937 rng(4);
    test_helpers::StructuredBuilder builder(rng);
    for (int iter = 0; iter < 300; ++iter) {
        const Graph g = iter % 2 ? builder.build(5 + iter % 40)
                                 : test_helpers::randomProgramGraph(3 + iter % 30, iter % 15, rng);
        const auto num = hammock::computeNumbering(g);
        for (const auto& h : hammock::findHammocksReference(g)) {
            const int lp = num.L[h.start];
            const int size = static_cast<int>(h.vertices.size());
            for (const int v : h.vertices) {
                REQUIRE(num.L[v] >= lp && num.L[v] < lp + size);
                REQUIRE(num.M[v] >= num.M[h.start]);
            }
            REQUIRE(num.M[h.end] < num.M[h.start] || num.L[h.end] == lp + size);
        }
    }
}

TEST(large_structured_graph_completes) {
    std::mt19937 rng(1);
    test_helpers::StructuredBuilder builder(rng);
    const Graph g = builder.build(3000);
    const auto result = hammock::findHammocks(g);
    CHECK(!result.spans.empty());
    // Весь граф без выхода — всегда гамак с началом во входе и концом в выходе.
    bool whole = false;
    for (const auto& span : result.spans) {
        if (span.start == g.entry() && span.end == g.exit() && span.size == g.vertexCount() - 1) whole = true;
    }
    CHECK(whole);
}

TEST(long_chain_hammock_count) {
    const int n = 1500;
    Graph g;
    for (int i = 0; i < n; ++i) g.addVertex(std::to_string(i));
    for (int i = 0; i + 1 < n; ++i) g.addEdge(i, i + 1);
    hammock::inferEntryExit(g);
    CHECK_EQ(hammock::findHammocks(g).spans.size(), static_cast<std::size_t>(n) * (n - 1) / 2);
}
