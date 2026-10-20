#pragma once

#include "hammock/graph.hpp"
#include "hammock/layout.hpp"

#include <string>

namespace hammock::layout {

struct SvgOptions {
    std::string title;             // заголовок над рисунком (пусто — без заголовка)
    bool showClusterLabels = true; // подписи гамаков «p → q»
    double cornerRadius = 6;       // скругление изломов дуг
};

// Рисует укладку в SVG: рамки гамаков (цвет зависит от глубины вложенности),
// дуги (обратные — другим цветом), вершины с подписями; вход и выход выделены.
std::string renderSvg(const Graph& g, const Drawing& drawing, const SvgOptions& options = {});

}  // namespace hammock::layout
