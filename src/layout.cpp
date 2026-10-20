#include "hammock/layout.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace hammock::layout {

std::size_t displayLength(const std::string& text) {
    std::size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
    }
    return count;
}

namespace {

constexpr double kEps = 1e-6;

struct Item {
    enum Kind { Vertex, Cluster, Terminal } kind;
    int id;  // вершина графа / индекс кластера / -1 для выходного порта
};

struct LevelEdge {
    int from;
    int to;
    std::vector<std::pair<int, int>> represents;
};

// Укладка содержимого одного кластера (в координатах его рамки).
struct Level {
    std::vector<Item> items;
    std::vector<sugiyama::NodeSpec> specs;
    std::vector<LevelEdge> edges;
    sugiyama::Result result;
    int entryItem = -1;
    int terminal = -1;
    double offsetX = 0;  // сдвиг содержимого внутри рамки
    double offsetY = 0;
    double width = 0;  // размер рамки
    double height = 0;
    double inPort = 0;  // порты относительно левого края рамки
    double outPort = 0;
};

class Composer {
public:
    Composer(const Graph& g, const LayoutOptions& options) : g_(g), opt_(options) {}

    Drawing run() {
        HammockTreeOptions treeOptions;
        treeOptions.useHammocks = opt_.useHammocks;
        tree_ = buildHammockTree(g_, treeOptions);  // проверяет, что граф — граф программы
        levels_.resize(tree_.clusters.size());
        vertexItem_.assign(g_.vertexCount(), -1);
        clusterItem_.assign(tree_.clusters.size(), -1);
        // Сначала укладываются меньшие гамаки: к моменту укладки кластера
        // размеры всех его дочерних гамаков уже известны.
        for (const int c : tree_.layoutOrder()) buildLevel(c);

        Drawing d;
        d.vertices.resize(g_.vertexCount());
        place(HammockTree::kRoot, opt_.margin, opt_.margin, d);
        d.width = levels_[HammockTree::kRoot].width + 2 * opt_.margin;
        d.height = levels_[HammockTree::kRoot].height + 2 * opt_.margin;
        d.tree = std::move(tree_);
        return d;
    }

private:
    const Graph& g_;
    const LayoutOptions& opt_;
    HammockTree tree_;
    std::vector<Level> levels_;
    std::vector<int> vertexItem_;
    std::vector<int> clusterItem_;

    // Дочерний кластер C, содержащий v, или -1, если v — собственная вершина C.
    int childContaining(int c, int v) const {
        int k = tree_.innermost[v];
        if (k == c) return -1;
        while (tree_.clusters[k].parent != c) k = tree_.clusters[k].parent;
        return k;
    }

    void buildLevel(int c) {
        Level& level = levels_[c];
        const auto& cluster = tree_.clusters[c];
        const bool isRoot = c == HammockTree::kRoot;

        for (const int v : cluster.ownVertices) {
            vertexItem_[v] = static_cast<int>(level.items.size());
            level.items.push_back({Item::Vertex, v});
            const double width = std::max(opt_.minNodeWidth, static_cast<double>(displayLength(g_.id(v))) *
                                                                     opt_.charWidth + 2 * opt_.nodePaddingX);
            level.specs.push_back({width, opt_.nodeHeight, -1, -1});
        }
        for (const int child : cluster.children) {
            clusterItem_[child] = static_cast<int>(level.items.size());
            level.items.push_back({Item::Cluster, child});
            const Level& inner = levels_[child];
            level.specs.push_back({inner.width, inner.height, inner.inPort, inner.outPort});
        }
        if (!isRoot) {
            level.terminal = static_cast<int>(level.items.size());
            level.items.push_back({Item::Terminal, -1});
            level.specs.push_back({0, 0, -1, -1});
        }

        auto itemOf = [&](int v) {
            if (!tree_.contains(c, v)) {
                // Дуги из гамака ведут только в его конечную вершину.
                if (isRoot || v != cluster.end) throw std::logic_error("layout: дуга выходит из гамака не в его конец");
                return level.terminal;
            }
            const int child = childContaining(c, v);
            return child < 0 ? vertexItem_[v] : clusterItem_[child];
        };

        std::map<std::pair<int, int>, std::size_t> index;
        for (const int u : tree_.members(c)) {
            for (const int v : g_.successors(u)) {
                const int a = itemOf(u);
                const int b = itemOf(v);
                if (a == b && !(u == v && level.items[a].kind == Item::Vertex)) continue;  // внутри дочернего гамака
                const auto key = std::make_pair(a, b);
                auto it = index.find(key);
                if (it == index.end()) {
                    it = index.emplace(key, level.edges.size()).first;
                    level.edges.push_back({a, b, {}});
                }
                level.edges[it->second].represents.emplace_back(u, v);
            }
        }

        sugiyama::Input input;
        input.nodes = level.specs;
        for (const auto& e : level.edges) {
            input.edges.push_back({e.from, e.to, static_cast<double>(e.represents.size())});
        }
        level.entryItem = itemOf(isRoot ? g_.entry() : cluster.start);
        input.source = level.entryItem;
        input.sink = level.terminal;
        level.result = sugiyama::layout(input, opt_.sugiyama);

        const auto& r = level.result;
        if (isRoot) {
            level.width = r.width;
            level.height = r.height;
            return;
        }
        level.offsetX = opt_.clusterPadding;
        level.offsetY = opt_.clusterPadding + opt_.clusterLabelHeight;
        level.width = r.width + 2 * opt_.clusterPadding;
        // Выходной порт (единственная вершина нижнего слоя) лежит на нижней границе рамки.
        level.height = level.offsetY + std::max(r.height, r.position[level.terminal].y);
        const auto& entrySpec = level.specs[level.entryItem];
        level.inPort = level.offsetX + r.position[level.entryItem].x +
                       (entrySpec.inPort < 0 ? entrySpec.width / 2 : entrySpec.inPort);
        level.outPort = level.offsetX + r.position[level.terminal].x;
    }

    // Продолжает дугу, вошедшую в рамку гамака k, до его начальной вершины
    // (возможно, через вложенные гамаки с тем же началом).
    void appendEntryPath(int k, double boxX, double boxY, std::vector<Point>& pts) const {
        const Level& level = levels_[k];
        const int e = level.entryItem;
        const double x = boxX + level.offsetX + level.result.position[e].x;
        const double y = boxY + level.offsetY + level.result.position[e].y;
        const Item& item = level.items[e];
        if (item.kind == Item::Cluster) {
            pts.push_back({x + levels_[item.id].inPort, y});
            appendEntryPath(item.id, x, y, pts);
        } else {
            pts.push_back({x + level.specs[e].width / 2, y});
        }
    }

    void place(int c, double boxX, double boxY, Drawing& d) const {
        const Level& level = levels_[c];
        const double ox = boxX + level.offsetX;
        const double oy = boxY + level.offsetY;
        const auto& r = level.result;
        for (std::size_t i = 0; i < level.items.size(); ++i) {
            const Item& item = level.items[i];
            const double x = ox + r.position[i].x;
            const double y = oy + r.position[i].y;
            if (item.kind == Item::Vertex) {
                d.vertices[item.id] = {x, y, level.specs[i].width, level.specs[i].height};
            } else if (item.kind == Item::Cluster) {
                const Level& inner = levels_[item.id];
                ClusterBox box;
                box.cluster = item.id;
                box.start = tree_.clusters[item.id].start;
                box.end = tree_.clusters[item.id].end;
                box.depth = tree_.clusters[item.id].depth;
                box.x = x;
                box.y = y;
                box.width = inner.width;
                box.height = inner.height;
                box.entryPort = {x + inner.inPort, y};
                box.exitPort = {x + inner.outPort, y + inner.height};
                d.clusters.push_back(box);
                place(item.id, x, y, d);
            }
        }
        for (std::size_t e = 0; e < level.edges.size(); ++e) {
            const LevelEdge& edge = level.edges[e];
            EdgeRoute route;
            route.level = c;
            route.represents = edge.represents;
            route.reversed = r.reversed[e];
            route.selfLoop = edge.from == edge.to;
            for (const auto& p : r.routes[e]) route.points.push_back({ox + p.x, oy + p.y});
            const Item& target = level.items[edge.to];
            if (target.kind == Item::Terminal) {
                route.arrow = false;
                const double bottom = boxY + level.height;
                if (!route.points.empty() && route.points.back().y < bottom - kEps) {
                    route.points.push_back({route.points.back().x, bottom});
                }
            } else if (target.kind == Item::Cluster && !route.selfLoop) {
                appendEntryPath(target.id, ox + r.position[edge.to].x, oy + r.position[edge.to].y, route.points);
            }
            d.edges.push_back(std::move(route));
        }
    }
};

}  // namespace

Drawing layoutGraph(const Graph& g, const LayoutOptions& options) { return Composer(g, options).run(); }

}  // namespace hammock::layout
