#include "hammock/hammock_tree.hpp"

#include <algorithm>
#include <climits>
#include <functional>

namespace hammock {

namespace {

// Разреженная таблица для запросов минимума/максимума на отрезке за O(1).
class SparseTable {
public:
    SparseTable(std::vector<int> values, std::function<int(int, int)> combine)
        : combine_(std::move(combine)) {
        const std::size_t n = values.size();
        table_.push_back(std::move(values));
        for (std::size_t len = 2; len <= n; len *= 2) {
            const auto& prev = table_.back();
            std::vector<int> next(n - len + 1);
            for (std::size_t i = 0; i + len <= n; ++i) next[i] = combine_(prev[i], prev[i + len / 2]);
            table_.push_back(std::move(next));
        }
    }

    // Значение на отрезке [lo, hi] (индексы включительно, lo <= hi).
    int query(int lo, int hi) const {
        int level = 0;
        while ((2 << level) <= hi - lo + 1) ++level;
        return combine_(table_[level][lo], table_[level][hi - (1 << level) + 1]);
    }

private:
    std::function<int(int, int)> combine_;
    std::vector<std::vector<int>> table_;
};

}  // namespace

std::vector<bool> findCrossingHammocks(const HammockResult& result) {
    // Гамак — отрезок [a, b] L-номеров. Отрезки [a, b] и [c, d] пересекаются
    // без вложения, если a < c <= b < d или c < a <= d < b. Первое проверяется
    // максимумом концов отрезков, начинающихся в (a, b], второе — минимумом
    // начал отрезков, кончающихся в [a, b).
    const int n = static_cast<int>(result.numbering.L.size());
    std::vector<int> maxEndByStart(n + 2, 0);
    std::vector<int> minStartByEnd(n + 2, INT_MAX);
    for (const auto& span : result.spans) {
        const int a = result.numbering.L[span.start];
        const int b = a + span.size - 1;
        maxEndByStart[a] = std::max(maxEndByStart[a], b);
        minStartByEnd[b] = std::min(minStartByEnd[b], a);
    }
    const SparseTable maxEnd(maxEndByStart, [](int x, int y) { return std::max(x, y); });
    const SparseTable minStart(minStartByEnd, [](int x, int y) { return std::min(x, y); });

    std::vector<bool> crossing(result.spans.size(), false);
    for (std::size_t i = 0; i < result.spans.size(); ++i) {
        const int a = result.numbering.L[result.spans[i].start];
        const int b = a + result.spans[i].size - 1;
        if (a == b) continue;
        crossing[i] = maxEnd.query(a + 1, b) > b || minStart.query(a, b - 1) < a;
    }
    return crossing;
}

HammockTree buildHammockTree(const Graph& g, const HammockResult& hammocks, const HammockTreeOptions& options) {
    const int n = g.vertexCount();
    HammockTree tree;
    tree.L = hammocks.numbering.L;
    tree.byL = hammocks.numbering.byL;

    HammockTree::Cluster root;
    root.firstL = 1;
    root.lastL = n;
    tree.clusters.push_back(root);

    if (options.useHammocks) {
        const std::vector<bool> crossing = findCrossingHammocks(hammocks);
        std::vector<HammockTree::Cluster> selected;
        for (std::size_t i = 0; i < hammocks.spans.size(); ++i) {
            const auto& span = hammocks.spans[i];
            if (crossing[i] || span.size < options.minSize) continue;
            if (!options.includeWholeProgram && span.start == g.entry() && span.end == g.exit()) continue;
            HammockTree::Cluster c;
            c.start = span.start;
            c.end = span.end;
            c.firstL = tree.L[span.start];
            c.lastL = c.firstL + span.size - 1;
            selected.push_back(c);
        }
        // Вложенные отрезки: внешний раньше внутреннего.
        std::sort(selected.begin(), selected.end(), [](const auto& x, const auto& y) {
            return x.firstL != y.firstL ? x.firstL < y.firstL : x.lastL > y.lastL;
        });
        for (auto& c : selected) tree.clusters.push_back(std::move(c));
    }

    // Родители: стек объемлющих отрезков.
    std::vector<int> stack{HammockTree::kRoot};
    for (int i = 1; i < static_cast<int>(tree.clusters.size()); ++i) {
        auto& c = tree.clusters[i];
        while (tree.clusters[stack.back()].lastL < c.firstL) stack.pop_back();
        c.parent = stack.back();
        c.depth = tree.clusters[c.parent].depth + 1;
        tree.clusters[c.parent].children.push_back(i);
        stack.push_back(i);
    }

    // Самый глубокий кластер для каждой вершины: проход по L-номерам.
    tree.innermost.assign(n, HammockTree::kRoot);
    stack.assign(1, HammockTree::kRoot);
    int next = 1;
    for (int l = 1; l <= n; ++l) {
        while (tree.clusters[stack.back()].lastL < l) stack.pop_back();
        while (next < static_cast<int>(tree.clusters.size()) && tree.clusters[next].firstL == l) {
            stack.push_back(next++);
        }
        const int v = tree.byL[l - 1];
        tree.innermost[v] = stack.back();
        tree.clusters[stack.back()].ownVertices.push_back(v);
    }
    return tree;
}

HammockTree buildHammockTree(const Graph& g, const HammockTreeOptions& options) {
    return buildHammockTree(g, findHammocks(g), options);
}

std::vector<int> HammockTree::members(int cluster) const {
    const Cluster& c = clusters[cluster];
    return std::vector<int>(byL.begin() + (c.firstL - 1), byL.begin() + c.lastL);
}

std::vector<int> HammockTree::layoutOrder() const {
    std::vector<int> order(clusters.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    // Потомок строго меньше предка, поэтому сортировка по размеру ставит
    // всех потомков раньше предков; корень — самый большой.
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (a == kRoot || b == kRoot) return b == kRoot && a != kRoot;
        return clusters[a].size() < clusters[b].size();
    });
    return order;
}

}  // namespace hammock
