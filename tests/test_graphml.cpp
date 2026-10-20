#include "hammock/graphml.hpp"
#include "hammock/xml.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <random>

using hammock::Graph;
using hammock::GraphError;
using hammock::GraphMLError;
using hammock::GraphMLOptions;
using hammock::loadGraphML;
using hammock::parseGraphML;

namespace {

const std::string kDataDir = HAMMOCK_TEST_DATA_DIR;

std::string wrap(const std::string& body, const std::string& keys = "", const std::string& edgeDefault = "directed") {
    return "<?xml version=\"1.0\"?>\n<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\">" + keys +
           "<graph id=\"G\" edgedefault=\"" + edgeDefault + "\">" + body + "</graph></graphml>";
}

}  // namespace

TEST(graphml_reads_nodes_and_edges_and_infers_entry_exit) {
    const Graph g = parseGraphML(wrap(
        "<node id='s'/><node id='a'/><node id='t'/>"
        "<edge source='s' target='a'/><edge id='e2' source='a' target='t'/>"));
    CHECK_EQ(g.vertexCount(), 3);
    CHECK_EQ(g.edgeCount(), 2);
    CHECK_EQ(g.id(g.entry()), std::string("s"));
    CHECK_EQ(g.id(g.exit()), std::string("t"));
    CHECK(g.hasEdge(*g.find("s"), *g.find("a")));
    CHECK(!g.hasEdge(*g.find("a"), *g.find("s")));
    hammock::validateProgramGraph(g);
}

TEST(graphml_ignores_parallel_edges) {
    const Graph g = parseGraphML(wrap(
        "<node id='s'/><node id='t'/><edge source='s' target='t'/><edge source='s' target='t'/>"));
    CHECK_EQ(g.edgeCount(), 1);
}

TEST(graphml_allows_edges_before_nodes) {
    const Graph g = parseGraphML(wrap("<edge source='s' target='t'/><node id='t'/><node id='s'/>"));
    CHECK_EQ(g.edgeCount(), 1);
    CHECK_EQ(g.id(g.entry()), std::string("s"));
}

TEST(graphml_reads_role_keys) {
    const std::string keys = "<key id='r' for='node' attr.name='role' attr.type='string'/>";
    // Без пометок вход не определить: у s1 и s2 нет входящих дуг.
    const Graph g = parseGraphML(wrap(
        "<node id='s1'><data key='r'>entry</data></node><node id='s2'/>"
        "<node id='x'/><node id='t'><data key='r'> EXIT </data></node>"
        "<edge source='s1' target='x'/><edge source='s2' target='x'/><edge source='x' target='t'/>",
        keys));
    CHECK_EQ(g.id(g.entry()), std::string("s1"));
    CHECK_EQ(g.id(g.exit()), std::string("t"));
    CHECK_THROWS(hammock::validateProgramGraph(g), GraphError);  // s2 недостижима из входа
}

TEST(graphml_reads_boolean_keys_with_defaults) {
    const std::string keys =
        "<key id='in' for='node' attr.name='entry' attr.type='boolean'><default>false</default></key>"
        "<key id='out' for='all' attr.name='exit' attr.type='boolean'/>"
        "<key id='other' for='edge' attr.name='role' attr.type='string'/>";
    const Graph g = parseGraphML(wrap(
        "<node id='a'><data key='in'>true</data></node><node id='b'><data key='out'>1</data></node>"
        "<edge source='a' target='b'><data key='other'>entry</data></edge>",
        keys));
    CHECK_EQ(g.id(g.entry()), std::string("a"));
    CHECK_EQ(g.id(g.exit()), std::string("b"));
}

TEST(graphml_options_override_file_roles) {
    GraphMLOptions options;
    options.entryId = "b";
    options.exitId = "c";
    const Graph g = parseGraphML(wrap(
        "<node id='a'/><node id='b'/><node id='c'/><edge source='a' target='b'/><edge source='b' target='c'/>"),
        options);
    CHECK_EQ(g.id(g.entry()), std::string("b"));
    CHECK_EQ(g.id(g.exit()), std::string("c"));
    options.entryId = "missing";
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/>"), options), GraphMLError);
}

TEST(graphml_leaves_entry_unset_when_ambiguous) {
    const Graph g = parseGraphML(wrap(
        "<node id='a'/><node id='b'/><node id='t'/><edge source='a' target='t'/><edge source='b' target='t'/>"));
    CHECK_EQ(g.entry(), -1);
    CHECK_EQ(g.id(g.exit()), std::string("t"));
    CHECK_THROWS(hammock::validateProgramGraph(g), GraphError);
}

TEST(graphml_accepts_documents_without_namespace_and_unicode_ids) {
    const Graph g = parseGraphML(
        "<graphml><graph edgedefault='directed'>"
        "<node id='вход'/><node id='выход'/><edge source='вход' target='выход' directed='true'/>"
        "</graph></graphml>");
    CHECK_EQ(g.id(g.entry()), std::string("вход"));
    CHECK_EQ(g.id(g.exit()), std::string("выход"));
}

TEST(graphml_accepts_directed_edges_in_undirected_graph) {
    const Graph g = parseGraphML(wrap("<node id='a'/><node id='b'/><edge source='a' target='b' directed='true'/>",
                                      "", "undirected"));
    CHECK_EQ(g.edgeCount(), 1);
}

TEST(graphml_rejects_invalid_documents) {
    CHECK_THROWS(parseGraphML("<notgraphml/>"), GraphMLError);
    CHECK_THROWS(parseGraphML("<graphml xmlns='urn:other'><graph/></graphml>"), GraphMLError);
    CHECK_THROWS(parseGraphML("<graphml xmlns='http://graphml.graphdrawing.org/xmlns'/>"), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node/>")), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/><node id='a'/>")), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/><edge source='a' target='b'/>")), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/><edge target='a'/>")), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/><node id='b'/><edge source='a' target='b'/>", "", "undirected")),
                 GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/><node id='b'/><edge source='a' target='b' directed='false'/>")),
                 GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/>", "", "sideways")), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'/><hyperedge><endpoint node='a'/></hyperedge>")), GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'><graph edgedefault='directed'/></node>")), GraphMLError);
    const std::string keys = "<key id='r' for='node' attr.name='role'/>";
    CHECK_THROWS(parseGraphML(wrap("<node id='a'><data key='r'>entry</data></node>"
                                   "<node id='b'><data key='r'>entry</data></node>",
                                   keys)),
                 GraphMLError);
    CHECK_THROWS(parseGraphML(wrap("<node id='a'><data key='r'>entry</data><data key='r'>exit</data></node>", keys)),
                 GraphMLError);
    CHECK_THROWS(parseGraphML("<graphml><graph>"), hammock::xml::XmlError);
}

TEST(graphml_loads_files) {
    const Graph ite = loadGraphML(kDataDir + "/if_then_else.graphml");
    CHECK_EQ(ite.vertexCount(), 6);
    CHECK_EQ(ite.edgeCount(), 6);
    CHECK_EQ(ite.id(ite.entry()), std::string("entry"));
    CHECK_EQ(ite.id(ite.exit()), std::string("exit"));

    const Graph yed = loadGraphML(kDataDir + "/yed_loop.graphml");
    CHECK_EQ(yed.vertexCount(), 4);
    CHECK_EQ(yed.edgeCount(), 4);
    CHECK_EQ(yed.id(yed.entry()), std::string("n0"));
    CHECK_EQ(yed.id(yed.exit()), std::string("n3"));

    const Graph nx = loadGraphML(kDataDir + "/networkx_style.graphml");
    CHECK_EQ(nx.vertexCount(), 5);
    CHECK_EQ(nx.id(nx.entry()), std::string("1"));
    CHECK_EQ(nx.id(nx.exit()), std::string("5"));

    CHECK_THROWS(loadGraphML(kDataDir + "/undirected.graphml"), GraphMLError);
    CHECK_THROWS(loadGraphML(kDataDir + "/does_not_exist.graphml"), GraphMLError);
}

TEST(graphml_loads_file_with_unicode_path) {
    const auto path = std::filesystem::temp_directory_path() / std::filesystem::u8path("граф_тест.graphml");
    {
        std::ofstream out(path, std::ios::binary);
        out << hammock::writeGraphML(test_helpers::makeGraph({"вход>выход"}));
    }
    const Graph g = loadGraphML(path.u8string());
    CHECK_EQ(g.vertexCount(), 2);
    CHECK_EQ(g.id(g.entry()), std::string("вход"));
    std::filesystem::remove(path);
}

TEST(graphml_write_then_parse_round_trip) {
    std::mt19937 rng(7);
    for (int iter = 0; iter < 50; ++iter) {
        Graph g = test_helpers::randomProgramGraph(2 + iter % 15, iter % 10, rng);
        if (iter % 3 == 0) g.addVertex("weird <&\"'> id");  // экранирование; граф уже не граф программы
        const Graph back = parseGraphML(hammock::writeGraphML(g));
        REQUIRE(back.vertexCount() == g.vertexCount());
        CHECK_EQ(back.edgeCount(), g.edgeCount());
        for (int v = 0; v < g.vertexCount(); ++v) {
            CHECK_EQ(back.id(v), g.id(v));
            CHECK(back.successors(v) == g.successors(v));
        }
        CHECK_EQ(back.entry(), g.entry());
        CHECK_EQ(back.exit(), g.exit());
    }
}

TEST(program_graph_validation) {
    using test_helpers::makeGraph;
    hammock::validateProgramGraph(makeGraph({"s>a", "a>t"}));

    Graph single;
    single.addVertex("x");
    CHECK_THROWS(hammock::validateProgramGraph(single), GraphError);

    // Нет входа: у всех вершин есть предшественники.
    CHECK_THROWS(hammock::validateProgramGraph(makeGraph({"a>b", "b>a", "b>t"})), GraphError);
    // Нет выхода.
    CHECK_THROWS(hammock::validateProgramGraph(makeGraph({"s>a", "a>b", "b>a"})), GraphError);

    // Дуга, заходящая во вход.
    Graph intoEntry = makeGraph({"s>a", "a>t", "a>s"});
    intoEntry.setEntry(*intoEntry.find("s"));
    CHECK_THROWS(hammock::validateProgramGraph(intoEntry), GraphError);

    // Дуга, исходящая из выхода.
    Graph fromExit = makeGraph({"s>a", "a>t", "t>a"});
    fromExit.setExit(*fromExit.find("t"));
    CHECK_THROWS(hammock::validateProgramGraph(fromExit), GraphError);

    // Вершина, из которой не достижим выход (тупиковый цикл).
    Graph deadLoop = makeGraph({"s>a", "a>t", "a>b", "b>c", "c>b"});
    CHECK_THROWS(hammock::validateProgramGraph(deadLoop), GraphError);

    // Вход совпадает с выходом.
    Graph same = makeGraph({"s>t"});
    same.setExit(same.entry());
    CHECK_THROWS(hammock::validateProgramGraph(same), GraphError);
}

TEST(graph_rejects_duplicate_vertices_and_bad_edges) {
    Graph g;
    g.addVertex("a");
    CHECK_THROWS(g.addVertex("a"), GraphError);
    CHECK_THROWS(g.addEdge(0, 5), GraphError);
    CHECK_THROWS(g.addEdge("a", "zzz"), GraphError);
    CHECK_THROWS(g.setEntry(3), GraphError);
}
