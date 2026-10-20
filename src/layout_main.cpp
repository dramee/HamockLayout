// Утилита: читает граф GraphML и рисует его в SVG на основе гамачного представления.
#include "cli_args.hpp"
#include "hammock/graphml.hpp"
#include "hammock/layout.hpp"
#include "hammock/svg.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void printUsage() {
    std::cout << "Использование: hammock_layout <граф.graphml> [параметры]\n"
                 "  -o FILE         записать SVG в файл (по умолчанию — в стандартный вывод)\n"
                 "  --flat          обычная укладка Сугиямы без выделения гамаков\n"
                 "  --title TEXT    заголовок рисунка\n"
                 "  --no-labels     не подписывать рамки гамаков\n"
                 "  --entry ID      id входной вершины\n"
                 "  --exit ID       id выходной вершины\n"
                 "  --stats         вывести в stderr сведения об укладке\n";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string input;
    std::string output;
    bool stats = false;
    hammock::GraphMLOptions graphOptions;
    hammock::layout::LayoutOptions layoutOptions;
    hammock::layout::SvgOptions svgOptions;
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    const int count = static_cast<int>(args.size());
    for (int i = 1; i < count; ++i) {
        const std::string& arg = args[i];
        const bool hasValue = i + 1 < count;
        if (arg == "-o" && hasValue) {
            output = args[++i];
        } else if (arg == "--title" && hasValue) {
            svgOptions.title = args[++i];
        } else if (arg == "--entry" && hasValue) {
            graphOptions.entryId = args[++i];
        } else if (arg == "--exit" && hasValue) {
            graphOptions.exitId = args[++i];
        } else if (arg == "--flat") {
            layoutOptions.useHammocks = false;
        } else if (arg == "--no-labels") {
            svgOptions.showClusterLabels = false;
        } else if (arg == "--stats") {
            stats = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (input.empty() && arg.rfind("-", 0) != 0) {
            input = arg;
        } else {
            std::cerr << "Неизвестный параметр: " << arg << "\n";
            printUsage();
            return 2;
        }
    }
    if (input.empty()) {
        printUsage();
        return 2;
    }

    try {
        const hammock::Graph g = hammock::loadGraphML(input, graphOptions);
        const auto drawing = hammock::layout::layoutGraph(g, layoutOptions);
        const std::string svg = hammock::layout::renderSvg(g, drawing, svgOptions);
        if (output.empty()) {
            std::cout << svg;
        } else {
            std::ofstream out(std::filesystem::u8path(output), std::ios::binary);
            if (!out) throw std::runtime_error("не удалось открыть файл '" + output + "' для записи");
            out << svg;
        }
        if (stats) {
            std::cerr << "вершин: " << g.vertexCount() << ", дуг: " << g.edgeCount()
                      << ", выделено гамаков: " << drawing.clusters.size() << ", размер: " << drawing.width << " x "
                      << drawing.height << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
