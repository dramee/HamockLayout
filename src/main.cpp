// Утилита командной строки: читает граф в формате GraphML и выводит его гамаки.
#include "cli_args.hpp"
#include "hammock/graphml.hpp"
#include "hammock/hammocks.hpp"
#include "hammock/xml.hpp"

#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void printUsage() {
    std::cout << "Использование: hammocks <граф.graphml> [параметры]\n"
                 "  --entry ID     id входной вершины\n"
                 "  --exit ID      id выходной вершины\n"
                 "  --json         вывод в формате JSON\n"
                 "  --numbering    дополнительно вывести M-, N- и L-нумерации\n"
                 "  --reference    использовать эталонный алгоритм O(n^2 m) вместо алгоритма Касьянова\n";
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

void printText(const hammock::Graph& g, const std::vector<hammock::Hammock>& hammocks,
               const hammock::Numbering* numbering) {
    std::cout << "Вершин: " << g.vertexCount() << ", дуг: " << g.edgeCount() << ", вход: " << g.id(g.entry())
              << ", выход: " << g.id(g.exit()) << "\n";
    if (numbering != nullptr) {
        std::cout << "Нумерации (вершина: M N L):\n";
        for (int v = 0; v < g.vertexCount(); ++v) {
            std::cout << "  " << g.id(v) << ": " << numbering->M[v] << ' ' << numbering->N[v] << ' '
                      << numbering->L[v] << "\n";
        }
    }
    std::cout << "Найдено гамаков: " << hammocks.size() << "\n";
    for (const auto& h : hammocks) {
        std::cout << "  начало " << g.id(h.start) << ", конец " << g.id(h.end) << ": {";
        for (std::size_t i = 0; i < h.vertices.size(); ++i) {
            std::cout << (i ? ", " : "") << g.id(h.vertices[i]);
        }
        std::cout << "}\n";
    }
}

void printJson(const hammock::Graph& g, const std::vector<hammock::Hammock>& hammocks,
               const hammock::Numbering* numbering) {
    std::cout << "{\n  \"vertices\": " << g.vertexCount() << ",\n  \"edges\": " << g.edgeCount()
              << ",\n  \"entry\": " << jsonString(g.id(g.entry())) << ",\n  \"exit\": " << jsonString(g.id(g.exit()))
              << ",\n";
    if (numbering != nullptr) {
        std::cout << "  \"numbering\": {";
        for (int v = 0; v < g.vertexCount(); ++v) {
            std::cout << (v ? ", " : "") << jsonString(g.id(v)) << ": {\"M\": " << numbering->M[v]
                      << ", \"N\": " << numbering->N[v] << ", \"L\": " << numbering->L[v] << "}";
        }
        std::cout << "},\n";
    }
    std::cout << "  \"hammocks\": [";
    for (std::size_t k = 0; k < hammocks.size(); ++k) {
        const auto& h = hammocks[k];
        std::cout << (k ? ",\n" : "\n") << "    {\"start\": " << jsonString(g.id(h.start))
                  << ", \"end\": " << jsonString(g.id(h.end)) << ", \"vertices\": [";
        for (std::size_t i = 0; i < h.vertices.size(); ++i) {
            std::cout << (i ? ", " : "") << jsonString(g.id(h.vertices[i]));
        }
        std::cout << "]}";
    }
    std::cout << (hammocks.empty() ? "]\n}\n" : "\n  ]\n}\n");
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string path;
    hammock::GraphMLOptions options;
    bool json = false;
    bool showNumbering = false;
    bool reference = false;
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    const int count = static_cast<int>(args.size());
    for (int i = 1; i < count; ++i) {
        const std::string& arg = args[i];
        if ((arg == "--entry" || arg == "--exit") && i + 1 < count) {
            (arg == "--entry" ? options.entryId : options.exitId) = args[++i];
        } else if (arg == "--json") {
            json = true;
        } else if (arg == "--numbering") {
            showNumbering = true;
        } else if (arg == "--reference") {
            reference = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (path.empty() && arg.rfind("--", 0) != 0) {
            path = arg;
        } else {
            std::cerr << "Неизвестный параметр: " << arg << "\n";
            printUsage();
            return 2;
        }
    }
    if (path.empty()) {
        printUsage();
        return 2;
    }

    try {
        const hammock::Graph g = hammock::loadGraphML(path, options);
        hammock::validateProgramGraph(g);
        hammock::HammockResult result;
        std::vector<hammock::Hammock> hammocks;
        if (reference) {
            hammocks = hammock::findHammocksReference(g);
            if (showNumbering) result.numbering = hammock::computeNumbering(g);
        } else {
            result = hammock::findHammocks(g);
            hammocks = result.hammocks();
        }
        const hammock::Numbering* numbering = showNumbering ? &result.numbering : nullptr;
        if (json) printJson(g, hammocks, numbering);
        else printText(g, hammocks, numbering);
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
