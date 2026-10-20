#pragma once

#include "hammock/graph.hpp"
#include "hammock/hammocks.hpp"

#include <vector>

// Иерархия гамаков для визуализации.
//
// Гамаки графа могут частично перекрываться: в цепочке a→b→c→d гамаки {a, b}
// и {b, c} пересекаются, но ни один не содержит другой. Для вложенной укладки
// нужно ламинарное семейство (любые два множества либо вложены, либо не
// пересекаются), поэтому берутся только гамаки, которые не пересекаются ни
// с одним другим гамаком графа. Такие гамаки соответствуют «каноническим»
// фрагментам программы: телам ветвлений, циклам и т. п., а цепочки остаются
// плоскими последовательностями внутри объемлющего гамака.
//
// По лемме 4 каждый гамак — это интервал L-номеров, поэтому пересечения
// проверяются на интервалах: за O(n log n + H), где H — число гамаков.
namespace hammock {

struct HammockTreeOptions {
    // false — дерево из одного корня (плоская укладка без гамаков).
    bool useHammocks = true;
    // Гамаки меньшего размера не выделяются (одиночная вершина — тривиальный гамак).
    int minSize = 2;
    // Выделять ли гамак «вся программа без выхода» (начало — вход, конец — выход).
    bool includeWholeProgram = false;
};

struct HammockTree {
    struct Cluster {
        int start = -1;  // начальная вершина гамака (-1 у корня)
        int end = -1;    // конечная вершина гамака (-1 у корня)
        int firstL = 1;  // вершины кластера — L-номера [firstL, lastL]
        int lastL = 0;
        int parent = -1;
        int depth = 0;
        std::vector<int> children;     // дочерние кластеры
        std::vector<int> ownVertices;  // вершины, не лежащие в дочерних кластерах
        int size() const { return lastL - firstL + 1; }
    };

    // clusters[0] — корень (весь граф), остальные — выделенные гамаки.
    std::vector<Cluster> clusters;
    // innermost[v] — самый глубокий кластер, содержащий вершину v.
    std::vector<int> innermost;
    // L-номера вершин (из нумерации алгоритма выделения гамаков).
    std::vector<int> L;
    // byL[k - 1] — вершина с L-номером k.
    std::vector<int> byL;

    static constexpr int kRoot = 0;

    bool contains(int cluster, int vertex) const {
        const Cluster& c = clusters[cluster];
        return L[vertex] >= c.firstL && L[vertex] <= c.lastL;
    }
    // Все вершины кластера в порядке L-номеров.
    std::vector<int> members(int cluster) const;
    // Порядок укладки: сначала меньшие гамаки, корень — последним.
    // Каждый кластер идёт после всех своих потомков.
    std::vector<int> layoutOrder() const;
};

HammockTree buildHammockTree(const Graph& g, const HammockResult& hammocks, const HammockTreeOptions& options = {});
HammockTree buildHammockTree(const Graph& g, const HammockTreeOptions& options = {});

// crossing[i] == true, если гамак result.spans[i] пересекается (перекрывается
// без вложения) хотя бы с одним другим гамаком графа.
std::vector<bool> findCrossingHammocks(const HammockResult& result);

}  // namespace hammock
