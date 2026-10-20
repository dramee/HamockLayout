#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hammock {

// Ошибка: граф не является графом программы в смысле статьи
// (нет входа/выхода, есть дуги во вход, недостижимые вершины и т. п.).
class GraphError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Ориентированный граф G = (X, Г), заданный списками смежности:
// у каждой вершины есть список преемников Г(v) и список предшественников Г^-1(v).
// Вершины нумеруются 0..n-1, исходные имена (id из GraphML) хранятся в ids.
class Graph {
public:
    // Добавляет вершину с именем id и возвращает её индекс.
    // Бросает GraphError, если вершина с таким именем уже есть.
    int addVertex(const std::string& id);

    // Добавляет дугу from -> to. Кратные дуги игнорируются
    // (на множество гамаков они не влияют).
    void addEdge(int from, int to);
    void addEdge(std::string_view from, std::string_view to);

    std::optional<int> find(std::string_view id) const;
    int vertexCount() const { return static_cast<int>(ids_.size()); }
    int edgeCount() const { return edgeCount_; }

    const std::string& id(int v) const { return ids_.at(v); }
    const std::vector<int>& successors(int v) const { return succ_.at(v); }
    const std::vector<int>& predecessors(int v) const { return pred_.at(v); }
    bool hasEdge(int from, int to) const;

    int entry() const { return entry_; }
    int exit() const { return exit_; }
    void setEntry(int v);
    void setExit(int v);

private:
    std::vector<std::string> ids_;
    std::unordered_map<std::string, int> index_;
    std::vector<std::vector<int>> succ_;
    std::vector<std::vector<int>> pred_;
    int edgeCount_ = 0;
    int entry_ = -1;
    int exit_ = -1;
};

// Проверяет, что граф является графом программы:
// выделены различные вход и выход, нет дуг, заходящих во вход или исходящих
// из выхода, и через каждую вершину проходит путь от входа к выходу.
// При нарушении бросает GraphError с описанием.
void validateProgramGraph(const Graph& g);

// Пытается определить вход и выход по степеням вершин: вход — единственная
// вершина без предшественников, выход — единственная вершина без преемников.
// Уже заданные вход/выход не меняет.
void inferEntryExit(Graph& g);

}  // namespace hammock
