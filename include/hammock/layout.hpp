#pragma once

#include "hammock/graph.hpp"
#include "hammock/hammock_tree.hpp"
#include "hammock/sugiyama.hpp"

#include <utility>
#include <vector>

// Визуализация графа программы на основе гамачного представления.
//
// Гамаки иерархии (см. hammock_tree.hpp) укладываются по очереди — от меньших
// к большим. Содержимое гамака (его собственные вершины и уже уложенные
// дочерние гамаки) укладывается алгоритмом Сугиямы, после чего гамак
// становится в объемлющем графе одной вершиной-прямоугольником:
//   * все дуги извне входят в гамак только в начальную вершину p, поэтому
//     входной порт прямоугольника стоит над p;
//   * все дуги из гамака ведут в конечную вершину q, поэтому внутри гамака
//     они сходятся в выходной порт на нижней границе прямоугольника, и из
//     него в объемлющем графе выходит одна дуга в q.
// Корень иерархии — весь граф.
namespace hammock::layout {

using Point = sugiyama::Point;

struct LayoutOptions {
    sugiyama::Options sugiyama;
    bool useHammocks = true;  // false — обычная (плоская) укладка Сугиямы
    double charWidth = 7.4;   // оценка ширины символа подписи
    double nodeHeight = 30;
    double nodePaddingX = 12;
    double minNodeWidth = 40;
    double clusterPadding = 14;      // отступ содержимого от рамки гамака
    double clusterLabelHeight = 16;  // место под подпись гамака
    double margin = 20;              // поля рисунка
};

struct VertexBox {
    double x = 0, y = 0, width = 0, height = 0;  // левый верхний угол и размеры
};

struct ClusterBox {
    int cluster = 0;  // индекс в HammockTree::clusters
    int start = -1;   // начальная вершина гамака
    int end = -1;     // конечная вершина гамака
    int depth = 0;
    double x = 0, y = 0, width = 0, height = 0;
    Point entryPort;  // точка входа на верхней границе (над начальной вершиной)
    Point exitPort;   // точка выхода на нижней границе
};

struct EdgeRoute {
    std::vector<Point> points;
    bool reversed = false;  // обратная дуга (идёт вверх)
    bool selfLoop = false;
    bool arrow = true;      // false — дуга кончается в выходном порту гамака
    int level = 0;          // кластер, в укладке которого проложена дуга
    // Исходные дуги графа (from, to), которые изображает этот маршрут.
    std::vector<std::pair<int, int>> represents;
};

struct Drawing {
    HammockTree tree;
    std::vector<VertexBox> vertices;   // по индексу вершины графа
    std::vector<ClusterBox> clusters;  // от внешних к внутренним
    std::vector<EdgeRoute> edges;
    double width = 0;
    double height = 0;
};

// Строит укладку графа программы. Бросает GraphError для некорректного графа.
Drawing layoutGraph(const Graph& g, const LayoutOptions& options = {});

// Ширина подписи в символах Unicode (UTF-8).
std::size_t displayLength(const std::string& text);

}  // namespace hammock::layout
