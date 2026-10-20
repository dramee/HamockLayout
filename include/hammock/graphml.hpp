#pragma once

#include "hammock/graph.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace hammock {

class GraphMLError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Пространство имён GraphML.
inline constexpr std::string_view kGraphMLNamespace = "http://graphml.graphdrawing.org/xmlns";

struct GraphMLOptions {
    // Явно заданные id входа и выхода (имеют приоритет над данными файла).
    std::string entryId;
    std::string exitId;
};

// Читает первый граф (<graph>) из документа GraphML.
//
// Вход и выход определяются в таком порядке:
//   1) GraphMLOptions::entryId / exitId;
//   2) данные вершин: ключ с attr.name="role" и значением "entry"/"exit"
//      либо логические ключи attr.name="entry" / attr.name="exit" ("true"/"1");
//   3) по степеням: единственная вершина без входящих дуг — вход,
//      единственная вершина без исходящих дуг — выход.
// Если определить вход/выход не удалось, они остаются незаданными
// (validateProgramGraph сообщит об ошибке).
//
// Элементы чужих пространств имён (например, графика yEd) игнорируются.
// Неориентированные дуги, гипердуги и вложенные графы не поддерживаются —
// для них бросается GraphMLError. Ошибки синтаксиса XML — xml::XmlError.
Graph parseGraphML(std::string_view document, const GraphMLOptions& options = {});

// Читает файл GraphML с диска.
Graph loadGraphML(const std::string& path, const GraphMLOptions& options = {});

// Записывает граф в GraphML (вход и выход помечаются данными role=entry/exit).
std::string writeGraphML(const Graph& g);

}  // namespace hammock
