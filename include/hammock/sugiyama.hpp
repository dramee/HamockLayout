#pragma once

#include <vector>

// Послойная укладка ориентированного графа (алгоритм Сугиямы):
//   1) удаление циклов — обращение обратных дуг обхода в глубину;
//   2) распределение по слоям — длиннейший путь от источника;
//   3) нормализация — длинные дуги разбиваются фиктивными вершинами;
//   4) упорядочивание в слоях — барицентрический метод с перестановками соседей;
//   5) координаты — итеративное выравнивание по соседям с сохранением порядка
//      (взвешенная изотоническая регрессия, алгоритм PAVA);
//   6) маршрутизация дуг по фиктивным вершинам.
// Вершины могут иметь разные размеры и порты — точки, в которые входят
// и из которых выходят дуги (это нужно для вершин-гамаков).
namespace hammock::sugiyama {

struct Point {
    double x = 0;
    double y = 0;
};

struct NodeSpec {
    double width = 0;
    double height = 0;
    // Смещение порта от левого края вершины: входящие дуги приходят в
    // (inPort, верх), исходящие выходят из (outPort, низ). < 0 — середина.
    double inPort = -1;
    double outPort = -1;
};

struct EdgeSpec {
    int from = 0;
    int to = 0;
    double weight = 1;  // кратность; влияет на выравнивание
};

struct Input {
    std::vector<NodeSpec> nodes;
    std::vector<EdgeSpec> edges;  // петли допускаются
    int source = -1;  // вершина, которая должна оказаться в верхнем слое (-1 — нет)
    int sink = -1;    // вершина, которая должна оказаться одна в нижнем слое (-1 — нет)
};

struct Options {
    double layerGap = 44;    // расстояние между слоями
    double nodeGap = 26;     // минимальный зазор между вершинами слоя
    double dummyGap = 12;    // зазор, если рядом фиктивная вершина
    double laneOffset = 10;  // отступ боковой «дорожки» обратных дуг и петель
    int orderingIterations = 24;
    int coordinateIterations = 16;
};

struct Result {
    // Левый верхний угол каждой вершины входа. Укладка нормирована:
    // все вершины и маршруты лежат в [0, width] × [0, height].
    std::vector<Point> position;
    std::vector<int> layer;  // слой вершины (0 — верхний)
    std::vector<int> order;  // позиция вершины в слое (с учётом фиктивных)
    int layerCount = 0;
    int dummyCount = 0;
    // Маршрут каждой дуги входа в её исходном направлении: от порта
    // источника к порту приёмника. Для петли — петля справа от вершины.
    std::vector<std::vector<Point>> routes;
    std::vector<bool> reversed;  // дуга была обращена при удалении циклов
    int crossings = 0;           // число пересечений дуг между соседними слоями
    double width = 0;
    double height = 0;
};

Result layout(const Input& input, const Options& options = {});

}  // namespace hammock::sugiyama
