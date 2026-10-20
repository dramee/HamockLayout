#include "hammock/hammocks.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace hammock {

// ---------------------------------------------------------------------------
// Нумерации
// ---------------------------------------------------------------------------

namespace {

// N- и M-нумерации строятся при построении пути из входа. Путь хранится в
// магазине из пар (вершина, позиция первого непросмотренного преемника).
// Для удлинения пути просматриваются преемники последней вершины до первой,
// ещё не включавшейся в путь; если такой нет, последняя вершина удаляется.
void computeMN(const Graph& g, Numbering& num) {
    const int n = g.vertexCount();
    num.M.assign(n, 0);
    num.N.assign(n, 0);
    std::vector<std::pair<int, std::size_t>> path;
    path.reserve(n);
    int included = 0;
    int removed = 0;
    num.M[g.entry()] = ++included;
    path.emplace_back(g.entry(), 0);
    while (!path.empty()) {
        const int v = path.back().first;
        std::size_t& next = path.back().second;
        const auto& succ = g.successors(v);
        if (next < succ.size()) {
            const int w = succ[next++];
            if (num.M[w] == 0) {
                num.M[w] = ++included;
                path.emplace_back(w, 0);
            }
        } else {
            // Первая удалённая вершина получает N = n, последняя (вход) — N = 1.
            num.N[v] = n - removed++;
            path.pop_back();
        }
    }
}

// L-нумерация вначале совпадает с N-нумерацией. Затем для p с L-номером
// 1, 2, ..., n строится S(p) — множество вершин, из которых p достижима в графе
// без вершин {q : Lq < Lp}. Вершинам S(p) в прежнем порядке присваиваются
// номера Lp, Lp + 1, ..., а остальным вершинам с Lq >= Lp (тоже в прежнем
// порядке) — следующие номера до n.
void computeL(const Graph& g, Numbering& num) {
    const int n = g.vertexCount();
    std::vector<int>& order = num.byL;  // order[k] — вершина на позиции k (L = k + 1)
    order.assign(n, 0);
    for (int v = 0; v < n; ++v) order[num.N[v] - 1] = v;
    std::vector<int> pos(n);
    for (int k = 0; k < n; ++k) pos[order[k]] = k;

    std::vector<int> inS(n, -1);  // inS[v] == i  <=>  v ∈ S(p) на шаге i
    std::vector<int> queue;
    queue.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int p = order[i];
        queue.assign(1, p);
        inS[p] = i;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            for (const int w : g.predecessors(queue[head])) {
                if (pos[w] >= i && inS[w] != i) {
                    inS[w] = i;
                    queue.push_back(w);
                }
            }
        }
        if (static_cast<int>(queue.size()) == n - i) continue;  // порядок не меняется
        std::stable_partition(order.begin() + i, order.end(), [&](int v) { return inS[v] == i; });
        for (int k = i; k < n; ++k) pos[order[k]] = k;
    }
    num.L.assign(n, 0);
    for (int k = 0; k < n; ++k) num.L[order[k]] = k + 1;
}

}  // namespace

Numbering computeNumbering(const Graph& g) {
    validateProgramGraph(g);
    Numbering num;
    computeMN(g, num);
    computeL(g, num);
    return num;
}

// ---------------------------------------------------------------------------
// Выделение гамаков (теорема 2)
// ---------------------------------------------------------------------------
//
// Для начальной вершины p рассматриваются множества U вершин с L-номерами
// Lp, Lp + 1, ..., Lp + |U| - 1 (лемма 4). Для них определяются
//   K(U) = {q ∈ Г(U) : Mq < Mp},
//   R(U) = {q ∈ Г^-1(U \ {p}) : Lq < Lp},
//   E(U) = max{Lq : q ∈ Г(U), Mq > Mp},
//   F(U) = max{Lq : q ∈ Г^-1(U \ {p})},
// и по лемме 5 U — гамак с началом p и концом q тогда и только тогда, когда
//   R(U) = ∅,  F(U) < Lp + |U|,  p ∉ Г(q)  и либо
//   K(U) = ∅,  Lq = Lp + |U| = E(U),   либо   K(U) = {q},  E(U) < Lp + |U|.
// При добавлении очередной вершины K, R, E, F обновляются по её
// предшественникам и преемникам, так что шаг для одной p занимает O(m).
//
// Все вершины гамака доминируются p, поэтому являются потомками p в дереве
// обхода и имеют M-номер не меньше Mp; встретив вершину с меньшим M-номером,
// расширение интервала можно прекратить. Так же прекращаем, если R(U) ≠ ∅
// или |K(U)| > 1: при расширении U эти множества только растут.
//
// Выход графа в гамак не включается (см. hammocks.hpp): для множеств,
// содержащих выход, лемма 4 неверна, поэтому, дойдя до выхода, интервал
// дальше не расширяем.

HammockResult findHammocks(const Graph& g) {
    HammockResult result;
    result.numbering = computeNumbering(g);
    const int n = g.vertexCount();
    const auto& M = result.numbering.M;
    const auto& L = result.numbering.L;
    const auto& byL = result.numbering.byL;

    std::vector<int> predOfP(n, -1);  // predOfP[v] == p  <=>  v ∈ Г^-1(p)
    std::vector<int> inK(n, -1);      // inK[v] == p  <=>  v ∈ K(U) для текущего p

    for (int lp = 1; lp <= n; ++lp) {
        const int p = byL[lp - 1];
        for (const int w : g.predecessors(p)) predOfP[w] = p;

        int E = 0;
        int F = 0;
        int kCount = 0;
        int kVertex = -1;
        for (int size = 1; lp + size - 1 <= n; ++size) {
            const int u = byL[lp + size - 2];  // вершина с L-номером lp + size - 1
            if (M[u] < M[p] || u == g.exit()) break;

            bool rNonEmpty = false;
            if (u != p) {
                for (const int w : g.predecessors(u)) {
                    if (L[w] < lp) {
                        rNonEmpty = true;
                        break;
                    }
                    F = std::max(F, L[w]);
                }
            }
            if (rNonEmpty) break;

            for (const int v : g.successors(u)) {
                if (M[v] < M[p]) {
                    if (inK[v] != p) {
                        inK[v] = p;
                        ++kCount;
                        kVertex = v;
                    }
                } else if (M[v] > M[p]) {
                    E = std::max(E, L[v]);
                }
            }
            if (kCount > 1) break;

            const int bound = lp + size;  // Lp + |U|
            if (F >= bound) continue;
            int q = -1;
            if (kCount == 0) {
                if (E == bound) q = byL[bound - 1];
            } else if (E < bound) {
                q = kVertex;
            }
            if (q < 0 || predOfP[q] == p) continue;  // требуется p ∉ Г(q)
            result.spans.push_back({p, q, size});
        }
    }
    return result;
}

std::vector<int> HammockResult::members(const HammockSpan& span) const {
    const int first = numbering.L.at(span.start) - 1;
    return std::vector<int>(numbering.byL.begin() + first, numbering.byL.begin() + first + span.size);
}

std::vector<Hammock> HammockResult::hammocks() const {
    std::vector<Hammock> out;
    out.reserve(spans.size());
    for (const auto& span : spans) out.push_back({span.start, span.end, members(span)});
    return canonical(std::move(out));
}

// ---------------------------------------------------------------------------
// Эталон и проверка определения
// ---------------------------------------------------------------------------

bool isHammock(const Graph& g, const std::vector<int>& U, int p, int q) {
    const int n = g.vertexCount();
    if (U.empty() || p < 0 || p >= n || q < 0 || q >= n) return false;
    std::vector<char> in(n, 0);
    for (const int v : U) {
        if (v < 0 || v >= n) return false;
        in[v] = 1;
    }
    if (!in[p] || in[q]) return false;
    bool reachesQ = false;
    for (const int u : U) {
        if (u != p) {
            for (const int w : g.predecessors(u)) {
                if (!in[w]) return false;  // Г^-1(U \ {p}) ⊆ U
            }
        }
        for (const int v : g.successors(u)) {
            if (in[v]) continue;
            if (v != q) return false;  // Г(U) \ U ⊆ {q}
            reachesQ = true;
        }
    }
    return reachesQ && !g.hasEdge(q, p);  // Г(U) \ U = {q},  p ∉ Г(q)
}

std::vector<Hammock> findHammocksReference(const Graph& g) {
    validateProgramGraph(g);
    const int n = g.vertexCount();
    std::vector<Hammock> out;
    std::vector<int> mark(n, -1);
    std::vector<int> stack;
    int stamp = 0;
    for (int p = 0; p < n; ++p) {
        for (int q = 0; q < n; ++q) {
            if (q == p) continue;
            // Если U — гамак с концом q, то U замкнуто по преемникам (кроме q),
            // а каждая вершина U достижима из p внутри U (путь из входа заходит
            // в U только через p). Значит, U — ровно множество вершин,
            // достижимых из p без прохода через q.
            ++stamp;
            std::vector<int> U{p};
            mark[p] = stamp;
            stack.assign(1, p);
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                for (const int w : g.successors(v)) {
                    if (w != q && mark[w] != stamp) {
                        mark[w] = stamp;
                        U.push_back(w);
                        stack.push_back(w);
                    }
                }
            }
            if (mark[g.exit()] == stamp) continue;  // выход не может лежать в гамаке
            if (isHammock(g, U, p, q)) out.push_back({p, q, std::move(U)});
        }
    }
    return canonical(std::move(out));
}

std::vector<Hammock> canonical(std::vector<Hammock> hammocks) {
    for (auto& h : hammocks) std::sort(h.vertices.begin(), h.vertices.end());
    std::sort(hammocks.begin(), hammocks.end());
    return hammocks;
}

}  // namespace hammock
