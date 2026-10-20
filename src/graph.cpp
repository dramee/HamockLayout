#include "hammock/graph.hpp"

#include <algorithm>

namespace hammock {

int Graph::addVertex(const std::string& id) {
    if (index_.count(id) != 0) {
        throw GraphError("вершина '" + id + "' объявлена повторно");
    }
    const int v = vertexCount();
    ids_.push_back(id);
    index_.emplace(id, v);
    succ_.emplace_back();
    pred_.emplace_back();
    return v;
}

void Graph::addEdge(int from, int to) {
    if (from < 0 || from >= vertexCount() || to < 0 || to >= vertexCount()) {
        throw GraphError("дуга ссылается на несуществующую вершину");
    }
    if (hasEdge(from, to)) {
        return;
    }
    succ_[from].push_back(to);
    pred_[to].push_back(from);
    ++edgeCount_;
}

void Graph::addEdge(std::string_view from, std::string_view to) {
    const auto f = find(from);
    const auto t = find(to);
    if (!f) throw GraphError("дуга ссылается на неизвестную вершину '" + std::string(from) + "'");
    if (!t) throw GraphError("дуга ссылается на неизвестную вершину '" + std::string(to) + "'");
    addEdge(*f, *t);
}

std::optional<int> Graph::find(std::string_view id) const {
    const auto it = index_.find(std::string(id));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

bool Graph::hasEdge(int from, int to) const {
    // Просматриваем более короткий из двух списков.
    const auto& s = succ_.at(from);
    const auto& p = pred_.at(to);
    if (s.size() <= p.size()) return std::find(s.begin(), s.end(), to) != s.end();
    return std::find(p.begin(), p.end(), from) != p.end();
}

void Graph::setEntry(int v) {
    if (v < 0 || v >= vertexCount()) throw GraphError("некорректная входная вершина");
    entry_ = v;
}

void Graph::setExit(int v) {
    if (v < 0 || v >= vertexCount()) throw GraphError("некорректная выходная вершина");
    exit_ = v;
}

namespace {

// Помечает вершины, достижимые из start по дугам, заданным функцией next.
template <typename Next>
std::vector<char> reach(const Graph& g, int start, Next next) {
    std::vector<char> seen(g.vertexCount(), 0);
    std::vector<int> stack{start};
    seen[start] = 1;
    while (!stack.empty()) {
        const int v = stack.back();
        stack.pop_back();
        for (const int w : next(v)) {
            if (!seen[w]) {
                seen[w] = 1;
                stack.push_back(w);
            }
        }
    }
    return seen;
}

}  // namespace

void validateProgramGraph(const Graph& g) {
    const int n = g.vertexCount();
    if (n < 2) throw GraphError("граф программы должен содержать хотя бы две вершины (вход и выход)");
    if (g.entry() < 0) throw GraphError("не задана входная вершина");
    if (g.exit() < 0) throw GraphError("не задана выходная вершина");
    if (g.entry() == g.exit()) throw GraphError("вход и выход должны быть различными вершинами");
    if (!g.predecessors(g.entry()).empty()) {
        throw GraphError("во входную вершину '" + g.id(g.entry()) + "' заходят дуги");
    }
    if (!g.successors(g.exit()).empty()) {
        throw GraphError("из выходной вершины '" + g.id(g.exit()) + "' исходят дуги");
    }
    const auto fromEntry = reach(g, g.entry(), [&](int v) -> const std::vector<int>& { return g.successors(v); });
    const auto toExit = reach(g, g.exit(), [&](int v) -> const std::vector<int>& { return g.predecessors(v); });
    for (int v = 0; v < n; ++v) {
        if (!fromEntry[v]) throw GraphError("вершина '" + g.id(v) + "' недостижима из входа");
        if (!toExit[v]) throw GraphError("из вершины '" + g.id(v) + "' недостижим выход");
    }
}

void inferEntryExit(Graph& g) {
    const int n = g.vertexCount();
    if (g.entry() < 0) {
        int found = -1, count = 0;
        for (int v = 0; v < n; ++v) {
            if (g.predecessors(v).empty()) {
                found = v;
                ++count;
            }
        }
        if (count == 1) g.setEntry(found);
    }
    if (g.exit() < 0) {
        int found = -1, count = 0;
        for (int v = 0; v < n; ++v) {
            if (g.successors(v).empty()) {
                found = v;
                ++count;
            }
        }
        if (count == 1) g.setExit(found);
    }
}

}  // namespace hammock
