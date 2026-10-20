#include "hammock/sugiyama.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hammock::sugiyama {

namespace {

constexpr double kEps = 1e-9;

using Neighbors = std::vector<std::vector<std::pair<int, double>>>;

class LayeredLayout {
public:
    LayeredLayout(const Input& input, const Options& options) : in_(input), opt_(options) {}

    Result run() {
        n_ = static_cast<int>(in_.nodes.size());
        for (const auto& e : in_.edges) {
            if (e.from < 0 || e.from >= n_ || e.to < 0 || e.to >= n_) {
                throw std::invalid_argument("sugiyama: дуга ссылается на несуществующую вершину");
            }
        }
        removeCycles();
        assignLayers();
        normalize();
        orderLayers();
        assignCoordinates();
        return buildResult();
    }

private:
    const Input& in_;
    const Options& opt_;
    int n_ = 0;  // число исходных вершин; фиктивные имеют номера n_, n_ + 1, ...

    std::vector<bool> reversed_;            // по дугам входа
    std::vector<std::vector<int>> chain_;   // по дугам входа: вершины от верхнего конца к нижнему
    std::vector<int> layer_;
    std::vector<bool> dummy_;
    std::vector<double> width_, height_, inOff_, outOff_, leftExtra_, rightExtra_;
    Neighbors up_, down_;  // соседи в предыдущем / следующем слое с весами
    std::vector<std::vector<int>> layers_;
    std::vector<int> pos_;
    std::vector<double> x_;  // центр вершины
    std::vector<double> bandTop_, bandHeight_;
    std::vector<double> edgeInX_, edgeOutX_;  // точки крепления прямых дуг (по дугам входа)
    int crossings_ = 0;

    bool isLoop(int e) const { return in_.edges[e].from == in_.edges[e].to; }

    // --- 1. Удаление циклов -------------------------------------------------
    void removeCycles() {
        const int m = static_cast<int>(in_.edges.size());
        std::vector<std::vector<int>> out(n_);
        std::vector<int> indegree(n_, 0);
        for (int e = 0; e < m; ++e) {
            if (isLoop(e)) continue;
            out[in_.edges[e].from].push_back(e);
            ++indegree[in_.edges[e].to];
        }
        reversed_.assign(m, false);
        std::vector<char> state(n_, 0);  // 0 — не посещена, 1 — в стеке, 2 — обработана
        std::vector<std::pair<int, std::size_t>> stack;
        auto dfs = [&](int root) {
            if (state[root] != 0) return;
            state[root] = 1;
            stack.assign(1, {root, 0});
            while (!stack.empty()) {
                const int v = stack.back().first;
                const std::size_t i = stack.back().second;
                if (i < out[v].size()) {
                    ++stack.back().second;
                    const int e = out[v][i];
                    const int w = in_.edges[e].to;
                    if (state[w] == 1) {
                        reversed_[e] = true;  // дуга в вершину на стеке — обратная
                    } else if (state[w] == 0) {
                        state[w] = 1;
                        stack.emplace_back(w, 0);
                    }
                } else {
                    state[v] = 2;
                    stack.pop_back();
                }
            }
        };
        if (in_.source >= 0) dfs(in_.source);
        for (int v = 0; v < n_; ++v) {
            if (indegree[v] == 0) dfs(v);
        }
        for (int v = 0; v < n_; ++v) dfs(v);
    }

    std::pair<int, int> dagEnds(int e) const {
        const auto& edge = in_.edges[e];
        return reversed_[e] ? std::make_pair(edge.to, edge.from) : std::make_pair(edge.from, edge.to);
    }

    // --- 2. Распределение по слоям -------------------------------------------
    void assignLayers() {
        const int m = static_cast<int>(in_.edges.size());
        std::vector<std::vector<int>> out(n_), in(n_);
        std::vector<int> indegree(n_, 0);
        for (int e = 0; e < m; ++e) {
            if (isLoop(e)) continue;
            const auto [a, b] = dagEnds(e);
            out[a].push_back(b);
            in[b].push_back(a);
            ++indegree[b];
        }
        std::vector<int> topo;
        topo.reserve(n_);
        for (int v = 0; v < n_; ++v) {
            if (indegree[v] == 0) topo.push_back(v);
        }
        for (std::size_t head = 0; head < topo.size(); ++head) {
            for (const int w : out[topo[head]]) {
                if (--indegree[w] == 0) topo.push_back(w);
            }
        }
        layer_.assign(n_, 0);
        for (const int v : topo) {
            for (const int w : out[v]) layer_[w] = std::max(layer_[w], layer_[v] + 1);
        }
        // Вершину, у которой исходящих дуг больше, чем входящих, опускаем
        // как можно ниже: суммарная длина дуг при этом уменьшается.
        for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
            const int v = *it;
            if (v == in_.source || v == in_.sink || out[v].empty() || out[v].size() <= in[v].size()) continue;
            int lowest = std::numeric_limits<int>::max();
            for (const int w : out[v]) lowest = std::min(lowest, layer_[w]);
            layer_[v] = std::max(layer_[v], lowest - 1);
        }
        if (in_.sink >= 0 && out[in_.sink].empty()) {
            int deepest = -1;
            for (int v = 0; v < n_; ++v) {
                if (v != in_.sink) deepest = std::max(deepest, layer_[v]);
            }
            layer_[in_.sink] = deepest + 1;
        }
    }

    // --- 3. Нормализация: фиктивные вершины ------------------------------------
    int addNode(int layer, bool isDummy, const NodeSpec& spec) {
        // Исходные вершины добавляются первыми (номера 0..n-1), затем фиктивные.
        const int id = static_cast<int>(dummy_.size());
        if (id < static_cast<int>(layer_.size())) {
            layer_[id] = layer;
        } else {
            layer_.push_back(layer);
        }
        dummy_.push_back(isDummy);
        width_.push_back(spec.width);
        height_.push_back(spec.height);
        inOff_.push_back(isDummy || spec.inPort < 0 ? 0 : spec.inPort - spec.width / 2);
        outOff_.push_back(isDummy || spec.outPort < 0 ? 0 : spec.outPort - spec.width / 2);
        leftExtra_.push_back(0);
        rightExtra_.push_back(0);
        up_.emplace_back();
        down_.emplace_back();
        return id;
    }

    static double segmentFactor(bool dummyA, bool dummyB) {
        if (dummyA && dummyB) return 8;
        if (dummyA || dummyB) return 2;
        return 1;
    }

    void normalize() {
        for (int v = 0; v < n_; ++v) addNode(layer_[v], false, in_.nodes[v]);
        const int m = static_cast<int>(in_.edges.size());
        chain_.assign(m, {});
        for (int e = 0; e < m; ++e) {
            const auto& edge = in_.edges[e];
            // Место под боковые «дорожки»: петли — справа, обратные дуги — с любой стороны.
            if (isLoop(e) || reversed_[e]) {
                rightExtra_[edge.from] = 2 * opt_.laneOffset;
                rightExtra_[edge.to] = 2 * opt_.laneOffset;
            }
            if (reversed_[e]) {
                leftExtra_[edge.from] = 2 * opt_.laneOffset;
                leftExtra_[edge.to] = 2 * opt_.laneOffset;
            }
            if (isLoop(e)) continue;
            const auto [a, b] = dagEnds(e);
            auto& chain = chain_[e];
            chain.push_back(a);
            for (int l = layer_[a] + 1; l < layer_[b]; ++l) chain.push_back(addNode(l, true, NodeSpec{}));
            chain.push_back(b);
            for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
                const int u = chain[i];
                const int w = chain[i + 1];
                const double weight = edge.weight * segmentFactor(dummy_[u], dummy_[w]);
                down_[u].emplace_back(w, weight);
                up_[w].emplace_back(u, weight);
            }
        }
    }

    int nodeCount() const { return static_cast<int>(dummy_.size()); }

    // --- 4. Упорядочивание вершин в слоях --------------------------------------
    void orderLayers() {
        const int total = nodeCount();
        int layerCount = 0;
        for (int v = 0; v < total; ++v) layerCount = std::max(layerCount, layer_[v] + 1);
        layers_.assign(layerCount, {});
        pos_.assign(total, 0);

        // Начальный порядок — порядок обхода в глубину от источника.
        std::vector<char> seen(total, 0);
        std::vector<int> stack;
        auto visit = [&](int root) {
            if (seen[root]) return;
            stack.assign(1, root);
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                if (seen[v]) continue;
                seen[v] = 1;
                pos_[v] = static_cast<int>(layers_[layer_[v]].size());
                layers_[layer_[v]].push_back(v);
                for (auto it = down_[v].rbegin(); it != down_[v].rend(); ++it) {
                    if (!seen[it->first]) stack.push_back(it->first);
                }
            }
        };
        if (in_.source >= 0) visit(in_.source);
        std::vector<int> roots(n_);
        for (int v = 0; v < n_; ++v) roots[v] = v;
        std::stable_sort(roots.begin(), roots.end(), [&](int a, int b) { return layer_[a] < layer_[b]; });
        for (const int v : roots) visit(v);

        auto best = layers_;
        int bestCrossings = totalCrossings();
        for (int it = 0; it < opt_.orderingIterations && bestCrossings > 0; ++it) {
            if (it % 2 == 0) {
                for (int l = 1; l < layerCount; ++l) sortByBarycenter(l, up_);
            } else {
                for (int l = layerCount - 2; l >= 0; --l) sortByBarycenter(l, down_);
            }
            for (int l = 0; l < layerCount; ++l) transpose(l);
            const int c = totalCrossings();
            if (c < bestCrossings) {
                bestCrossings = c;
                best = layers_;
            }
        }
        layers_ = std::move(best);
        for (const auto& layer : layers_) {
            for (std::size_t i = 0; i < layer.size(); ++i) pos_[layer[i]] = static_cast<int>(i);
        }
        crossings_ = bestCrossings;
    }

    void sortByBarycenter(int l, const Neighbors& neighbors) {
        auto& layer = layers_[l];
        std::vector<std::pair<double, int>> keyed;
        keyed.reserve(layer.size());
        for (const int v : layer) {
            double sum = 0;
            double weight = 0;
            for (const auto& [u, w] : neighbors[v]) {
                sum += w * pos_[u];
                weight += w;
            }
            keyed.emplace_back(weight > 0 ? sum / weight : pos_[v], v);
        }
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < layer.size(); ++i) {
            layer[i] = keyed[i].second;
            pos_[layer[i]] = static_cast<int>(i);
        }
    }

    // Число пересечений дуг, инцидентных u и v, если u стоит левее v.
    int pairCrossings(int u, int v) const {
        int count = 0;
        for (const Neighbors* nb : {&up_, &down_}) {
            for (const auto& a : (*nb)[u]) {
                for (const auto& b : (*nb)[v]) {
                    if (pos_[a.first] > pos_[b.first]) ++count;
                }
            }
        }
        return count;
    }

    void transpose(int l) {
        auto& layer = layers_[l];
        for (int pass = 0; pass < 8; ++pass) {
            bool improved = false;
            for (std::size_t i = 0; i + 1 < layer.size(); ++i) {
                const int u = layer[i];
                const int v = layer[i + 1];
                if (pairCrossings(v, u) < pairCrossings(u, v)) {
                    std::swap(layer[i], layer[i + 1]);
                    pos_[layer[i]] = static_cast<int>(i);
                    pos_[layer[i + 1]] = static_cast<int>(i + 1);
                    improved = true;
                }
            }
            if (!improved) break;
        }
    }

    int totalCrossings() const {
        int total = 0;
        for (std::size_t l = 0; l + 1 < layers_.size(); ++l) {
            std::vector<std::pair<int, int>> segments;
            for (const int u : layers_[l]) {
                for (const auto& d : down_[u]) segments.emplace_back(pos_[u], pos_[d.first]);
            }
            std::sort(segments.begin(), segments.end());
            // Инверсии по нижним концам — дерево Фенвика.
            const std::size_t size = layers_[l + 1].size();
            std::vector<int> fenwick(size + 1, 0);
            int seen = 0;
            for (const auto& s : segments) {
                int notGreater = 0;
                for (int i = s.second + 1; i > 0; i -= i & -i) notGreater += fenwick[i];
                total += seen - notGreater;
                for (int i = s.second + 1; i <= static_cast<int>(size); i += i & -i) ++fenwick[i];
                ++seen;
            }
        }
        return total;
    }

    // --- 5. Координаты ---------------------------------------------------------
    double separation(int a, int b) const {
        const double gap = (dummy_[a] || dummy_[b]) ? opt_.dummyGap : opt_.nodeGap;
        return width_[a] / 2 + rightExtra_[a] + leftExtra_[b] + width_[b] / 2 + gap;
    }

    void assignCoordinates() {
        x_.assign(nodeCount(), 0);
        for (const auto& layer : layers_) {
            double cursor = 0;
            for (std::size_t i = 0; i < layer.size(); ++i) {
                if (i > 0) cursor += separation(layer[i - 1], layer[i]);
                x_[layer[i]] = cursor;
            }
            if (!layer.empty()) {
                const double shift = (cursor + width_[layer.back()] / 2 - width_[layer.front()] / 2) / 2;
                for (const int v : layer) x_[v] -= shift;
            }
        }
        const int layerCount = static_cast<int>(layers_.size());
        const int rounds = opt_.coordinateIterations;
        for (int r = 0; r < rounds; ++r) {
            if (r >= rounds - 4) {
                for (int l = 0; l < layerCount; ++l) placeLayer(l, true, true);
            } else if (r % 2 == 0) {
                for (int l = 1; l < layerCount; ++l) placeLayer(l, true, false);
            } else {
                for (int l = layerCount - 2; l >= 0; --l) placeLayer(l, false, true);
            }
        }
    }

    // Ставит вершины слоя как можно ближе к желаемым позициям (взвешенное
    // среднее положений соседей с учётом портов), сохраняя порядок и зазоры:
    // минимизация Σ w_i (x_i - d_i)² при x_{i+1} >= x_i + sep_i.
    void placeLayer(int l, bool useUp, bool useDown) {
        const auto& layer = layers_[l];
        const std::size_t m = layer.size();
        if (m == 0) return;
        std::vector<double> target(m), weight(m), offset(m, 0);
        for (std::size_t i = 0; i < m; ++i) {
            const int v = layer[i];
            double sum = 0;
            double w = 0;
            if (useUp) {
                for (const auto& [u, wt] : up_[v]) {
                    sum += wt * (x_[u] + outOff_[u] - inOff_[v]);
                    w += wt;
                }
            }
            if (useDown) {
                for (const auto& [u, wt] : down_[v]) {
                    sum += wt * (x_[u] + inOff_[u] - outOff_[v]);
                    w += wt;
                }
            }
            if (w > 0) {
                target[i] = sum / w;
                weight[i] = w;
            } else {
                target[i] = x_[v];
                weight[i] = 0.01;
            }
            if (i > 0) offset[i] = offset[i - 1] + separation(layer[i - 1], v);
        }
        // Замена y_i = x_i - offset_i сводит задачу к изотонической регрессии.
        struct Block {
            double weight;
            double weightedSum;
            std::size_t count;
            double mean() const { return weightedSum / weight; }
        };
        std::vector<Block> blocks;
        for (std::size_t i = 0; i < m; ++i) {
            Block b{weight[i], weight[i] * (target[i] - offset[i]), 1};
            while (!blocks.empty() && blocks.back().mean() > b.mean()) {
                b.weight += blocks.back().weight;
                b.weightedSum += blocks.back().weightedSum;
                b.count += blocks.back().count;
                blocks.pop_back();
            }
            blocks.push_back(b);
        }
        std::size_t i = 0;
        for (const auto& b : blocks) {
            for (std::size_t k = 0; k < b.count; ++k, ++i) x_[layer[i]] = b.mean() + offset[i];
        }
    }

    // --- 6. Маршруты и результат -------------------------------------------------
    double top(int v) const { return bandTop_[layer_[v]] + (bandHeight_[layer_[v]] - height_[v]) / 2; }
    double bottom(int v) const { return top(v) + height_[v]; }
    double bandBottom(int l) const { return bandTop_[l] + bandHeight_[l]; }
    double inX(int v) const { return x_[v] + inOff_[v]; }
    double outX(int v) const { return x_[v] + outOff_[v]; }
    double right(int v) const { return x_[v] + width_[v] / 2; }
    double left(int v) const { return x_[v] - width_[v] / 2; }
    // Боковая «дорожка» обратной дуги у вершины v: справа (side > 0) или слева.
    double lane(int v, int side) const {
        return side > 0 ? right(v) + opt_.laneOffset : left(v) - opt_.laneOffset;
    }

    std::vector<Point> route(int e, int side) const {
        const auto& edge = in_.edges[e];
        const double s = opt_.laneOffset;
        std::vector<Point> pts;
        if (isLoop(e)) {
            const int v = edge.from;
            const double cy = top(v) + height_[v] / 2;
            const double dy = std::min(6.0, height_[v] / 3);
            pts = {{right(v), cy - dy}, {right(v) + s, cy - dy}, {right(v) + s, cy + dy}, {right(v), cy + dy}};
            return pts;
        }
        const auto& chain = chain_[e];
        if (!reversed_[e]) {
            const int a = chain.front();
            const int b = chain.back();
            const double ax = edgeOutX_[e];
            const double bx = edgeInX_[e];
            pts.push_back({ax, bottom(a)});
            if (bottom(a) < bandBottom(layer_[a]) - kEps) pts.push_back({ax, bandBottom(layer_[a])});
            for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
                const int d = chain[i];
                pts.push_back({x_[d], bandTop_[layer_[d]]});
                if (bandHeight_[layer_[d]] > kEps) pts.push_back({x_[d], bandBottom(layer_[d])});
            }
            if (top(b) > bandTop_[layer_[b]] + kEps) pts.push_back({bx, bandTop_[layer_[b]]});
            pts.push_back({bx, top(b)});
            return pts;
        }
        // Обращённая дуга u -> v (v выше u): выходим из нижнего порта u, огибаем u
        // сбоку, поднимаемся по фиктивным вершинам и входим в верхний порт v,
        // обогнув v с той же стороны.
        const int u = edge.from;
        const int v = edge.to;
        const double halfGap = opt_.layerGap / 2;
        const double ux = sideAttach(u, side, false);
        const double vx = sideAttach(v, side, true);
        pts.push_back({ux, bottom(u)});
        pts.push_back({ux, bottom(u) + s});
        pts.push_back({lane(u, side), bottom(u) + s});
        pts.push_back({lane(u, side), bandTop_[layer_[u]] - halfGap});
        for (std::size_t i = chain.size() - 2; i >= 1; --i) {
            const int d = chain[i];
            pts.push_back({x_[d], bandBottom(layer_[d])});
            if (bandHeight_[layer_[d]] > kEps) pts.push_back({x_[d], bandTop_[layer_[d]]});
        }
        pts.push_back({lane(v, side), bandBottom(layer_[v]) + halfGap});
        pts.push_back({lane(v, side), top(v) - s});
        pts.push_back({vx, top(v) - s});
        pts.push_back({vx, top(v)});
        return pts;
    }

    bool freePorts(int v, bool incoming) const {
        if (v >= n_ || width_[v] <= 0) return false;
        return (incoming ? in_.nodes[v].inPort : in_.nodes[v].outPort) < 0;
    }

    // Точка крепления обратной дуги: ближе к стороне её дорожки (если порт не задан).
    double sideAttach(int v, int side, bool incoming) const {
        if (!freePorts(v, incoming)) return incoming ? inX(v) : outX(v);
        return x_[v] + side * width_[v] * 0.35;
    }

    // Прямые дуги, входящие в вершину со свободными портами (или выходящие из
    // неё), распределяются вдоль её верхней (нижней) стороны в порядке
    // положения соседних концов, чтобы стрелки не сливались в одну точку.
    void spreadPorts() {
        const int m = static_cast<int>(in_.edges.size());
        edgeInX_.assign(m, 0);
        edgeOutX_.assign(m, 0);
        std::vector<std::vector<int>> incoming(n_), outgoing(n_);
        for (int e = 0; e < m; ++e) {
            if (isLoop(e) || reversed_[e]) continue;
            const auto& chain = chain_[e];
            edgeOutX_[e] = outX(chain.front());
            edgeInX_[e] = inX(chain.back());
            outgoing[chain.front()].push_back(e);
            incoming[chain.back()].push_back(e);
        }
        auto spread = [&](int v, std::vector<int>& edges, bool in) {
            if (edges.size() < 2 || !freePorts(v, in)) return;
            auto neighbourX = [&](int e) {
                const auto& chain = chain_[e];
                return in ? x_[chain[chain.size() - 2]] : x_[chain[1]];
            };
            std::stable_sort(edges.begin(), edges.end(), [&](int a, int b) { return neighbourX(a) < neighbourX(b); });
            const double k = static_cast<double>(edges.size());
            const double span = std::min(width_[v] * 0.6, (k - 1) * 14);
            for (std::size_t i = 0; i < edges.size(); ++i) {
                const double x = x_[v] - span / 2 + span * static_cast<double>(i) / (k - 1);
                (in ? edgeInX_ : edgeOutX_)[edges[i]] = x;
            }
        };
        for (int v = 0; v < n_; ++v) {
            spread(v, incoming[v], true);
            spread(v, outgoing[v], false);
        }
    }

    static double orientation(const Point& a, const Point& b, const Point& c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }

    // Число собственных пересечений ломаной с остальными маршрутами.
    static int countCrossings(const std::vector<Point>& path, const std::vector<std::vector<Point>>& routes) {
        int count = 0;
        for (const auto& other : routes) {
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                for (std::size_t j = 0; j + 1 < other.size(); ++j) {
                    const Point &a = path[i], &b = path[i + 1], &c = other[j], &d = other[j + 1];
                    if (orientation(a, b, c) * orientation(a, b, d) < -kEps &&
                        orientation(c, d, a) * orientation(c, d, b) < -kEps) {
                        ++count;
                    }
                }
            }
        }
        return count;
    }

    Result buildResult() {
        const int layerCount = static_cast<int>(layers_.size());
        bandHeight_.assign(layerCount, 0);
        bandTop_.assign(layerCount, 0);
        for (int v = 0; v < nodeCount(); ++v) {
            if (!dummy_[v]) bandHeight_[layer_[v]] = std::max(bandHeight_[layer_[v]], height_[v]);
        }
        for (int l = 1; l < layerCount; ++l) bandTop_[l] = bandTop_[l - 1] + bandHeight_[l - 1] + opt_.layerGap;

        Result r;
        r.layerCount = layerCount;
        r.dummyCount = nodeCount() - n_;
        r.crossings = crossings_;
        r.reversed = reversed_;
        r.layer.assign(layer_.begin(), layer_.begin() + n_);
        r.order.assign(pos_.begin(), pos_.begin() + n_);
        r.position.resize(n_);
        for (int v = 0; v < n_; ++v) r.position[v] = {x_[v] - width_[v] / 2, top(v)};
        r.routes.resize(in_.edges.size());
        spreadPorts();
        // Сначала прямые дуги и петли, затем обратные: для каждой обратной дуги
        // выбирается сторона обхода с меньшим числом пересечений с уже проложенными.
        const int m = static_cast<int>(in_.edges.size());
        for (int e = 0; e < m; ++e) {
            if (!reversed_[e]) r.routes[e] = route(e, 1);
        }
        for (int e = 0; e < m; ++e) {
            if (!reversed_[e]) continue;
            auto rightRoute = route(e, 1);
            auto leftRoute = route(e, -1);
            r.routes[e] = countCrossings(leftRoute, r.routes) < countCrossings(rightRoute, r.routes)
                              ? std::move(leftRoute)
                              : std::move(rightRoute);
        }

        // Нормировка: всё содержимое — в [0, width] × [0, height].
        double minX = std::numeric_limits<double>::max(), minY = minX;
        double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
        auto extend = [&](double x, double y) {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        };
        for (int v = 0; v < n_; ++v) {
            extend(r.position[v].x, r.position[v].y);
            extend(r.position[v].x + width_[v], r.position[v].y + height_[v]);
        }
        for (const auto& pts : r.routes) {
            for (const auto& p : pts) extend(p.x, p.y);
        }
        if (n_ == 0) minX = minY = maxX = maxY = 0;
        for (auto& p : r.position) {
            p.x -= minX;
            p.y -= minY;
        }
        for (auto& pts : r.routes) {
            for (auto& p : pts) {
                p.x -= minX;
                p.y -= minY;
            }
        }
        r.width = maxX - minX;
        r.height = maxY - minY;
        return r;
    }
};

}  // namespace

Result layout(const Input& input, const Options& options) { return LayeredLayout(input, options).run(); }

}  // namespace hammock::sugiyama
