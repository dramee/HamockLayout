#pragma once

#include "hammock/graph.hpp"
#include "hammock/hammocks.hpp"

#include <algorithm>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

namespace test_helpers {

using hammock::Graph;
using hammock::Hammock;

// Строит граф по списку дуг вида "a>b". Вершины создаются в порядке появления,
// преемники — в порядке перечисления дуг. Вход и выход определяются по степеням.
inline Graph makeGraph(std::initializer_list<const char*> edges) {
    Graph g;
    for (const std::string e : edges) {
        const auto sep = e.find('>');
        const std::string from = e.substr(0, sep);
        const std::string to = e.substr(sep + 1);
        if (!g.find(from)) g.addVertex(from);
        if (!g.find(to)) g.addVertex(to);
        g.addEdge(from, to);
    }
    hammock::inferEntryExit(g);
    return g;
}

// Текстовое представление гамака: "p>q{a,b,c}" (вершины по алфавиту).
inline std::string describe(const Graph& g, const Hammock& h) {
    std::vector<std::string> names;
    for (const int v : h.vertices) names.push_back(g.id(v));
    std::sort(names.begin(), names.end());
    std::string s = g.id(h.start) + ">" + g.id(h.end) + "{";
    for (std::size_t i = 0; i < names.size(); ++i) s += (i ? "," : "") + names[i];
    return s + "}";
}

// Отсортированное текстовое представление множества гамаков; не зависит
// от внутренней нумерации вершин, только от их имён.
inline std::string describe(const Graph& g, const std::vector<Hammock>& hammocks) {
    std::vector<std::string> items;
    for (const auto& h : hammocks) items.push_back(describe(g, h));
    std::sort(items.begin(), items.end());
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) s += (i ? " " : "") + items[i];
    return s;
}

inline std::string kasyanov(const Graph& g) { return describe(g, hammock::findHammocks(g).hammocks()); }
inline std::string reference(const Graph& g) { return describe(g, hammock::findHammocksReference(g)); }

// Случайный граф программы: вход v0, выход v{n-1}. Сначала строится каркас,
// гарантирующий, что каждая вершина лежит на пути от входа к выходу, затем
// добавляются extra случайных дуг (в том числе обратных и петель).
// Порядок вершин и дуг в графе перемешивается.
inline Graph randomProgramGraph(int n, int extra, std::mt19937& rng) {
    auto uniform = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    std::vector<std::pair<int, int>> edges;
    if (n == 2) edges.emplace_back(0, 1);
    for (int v = 1; v <= n - 2; ++v) {
        edges.emplace_back(uniform(0, v - 1), v);
        edges.emplace_back(v, uniform(v + 1, n - 1));
    }
    for (int i = 0; i < extra; ++i) edges.emplace_back(uniform(0, n - 2), uniform(1, n - 1));

    std::vector<int> order(n);
    for (int v = 0; v < n; ++v) order[v] = v;
    std::shuffle(order.begin(), order.end(), rng);
    std::shuffle(edges.begin(), edges.end(), rng);

    Graph g;
    for (const int v : order) g.addVertex("v" + std::to_string(v));
    for (const auto& [a, b] : edges) g.addEdge("v" + std::to_string(a), "v" + std::to_string(b));
    g.setEntry(*g.find("v0"));
    g.setExit(*g.find("v" + std::to_string(n - 1)));
    return g;
}

// Случайный структурированный граф программы из последовательностей,
// ветвлений if/else, циклов while и repeat. В таких графах много гамаков.
class StructuredBuilder {
public:
    explicit StructuredBuilder(std::mt19937& rng) : rng_(rng) {}

    Graph build(int budget) {
        g_ = Graph();
        counter_ = 0;
        const int entry = node();
        const int last = sequence(entry, budget);
        const int exit = node();
        g_.addEdge(last, exit);
        g_.setEntry(entry);
        g_.setExit(exit);
        return std::move(g_);
    }

private:
    std::mt19937& rng_;
    Graph g_;
    int counter_ = 0;

    int uniform(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    int node() { return g_.addVertex("n" + std::to_string(counter_++)); }

    // Последовательность операторов после вершины from; возвращает последнюю вершину.
    int sequence(int from, int budget) {
        int current = from;
        while (budget > 0) {
            const int cost = uniform(1, budget);
            current = statement(current, cost);
            budget -= cost;
        }
        return current;
    }

    int statement(int from, int budget) {
        const int kind = budget < 3 ? 0 : uniform(0, 3);
        switch (kind) {
            case 1: {  // if (c) then ... else ...
                const int c = node();
                g_.addEdge(from, c);
                const int thenBudget = uniform(0, budget - 2);
                const int thenEnd = sequence(c, thenBudget);
                const int elseEnd = sequence(c, budget - 2 - thenBudget);
                const int join = node();
                g_.addEdge(thenEnd, join);
                g_.addEdge(elseEnd, join);
                return join;
            }
            case 2: {  // while (c) body
                const int c = node();
                g_.addEdge(from, c);
                const int bodyEnd = sequence(c, budget - 1);
                g_.addEdge(bodyEnd, c);
                return c;
            }
            case 3: {  // repeat body until (c)
                const int start = node();
                g_.addEdge(from, start);
                const int bodyEnd = sequence(start, budget - 2);
                const int c = node();
                g_.addEdge(bodyEnd, c);
                g_.addEdge(c, start);
                return c;
            }
            default: {  // простой оператор
                const int v = node();
                g_.addEdge(from, v);
                return v;
            }
        }
    }
};

// Копия графа с перемешанным порядком вершин и дуг (имена сохраняются).
// Обход в глубину пойдёт в другом порядке, но множество гамаков не должно измениться.
inline Graph shuffled(const Graph& g, std::mt19937& rng) {
    std::vector<int> order(g.vertexCount());
    for (int v = 0; v < g.vertexCount(); ++v) order[v] = v;
    std::shuffle(order.begin(), order.end(), rng);
    std::vector<std::pair<int, int>> edges;
    for (int v = 0; v < g.vertexCount(); ++v) {
        for (const int w : g.successors(v)) edges.emplace_back(v, w);
    }
    std::shuffle(edges.begin(), edges.end(), rng);
    Graph out;
    for (const int v : order) out.addVertex(g.id(v));
    for (const auto& [a, b] : edges) out.addEdge(g.id(a), g.id(b));
    out.setEntry(*out.find(g.id(g.entry())));
    out.setExit(*out.find(g.id(g.exit())));
    return out;
}

}  // namespace test_helpers
