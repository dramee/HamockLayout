#include "hammock/graphml.hpp"
#include "hammock/hammocks.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

using hammock::Graph;
using hammock::isHammock;
using test_helpers::kasyanov;
using test_helpers::makeGraph;
using test_helpers::reference;

namespace {

const std::string kDataDir = HAMMOCK_TEST_DATA_DIR;

std::vector<int> vertices(const Graph& g, std::initializer_list<const char*> names) {
    std::vector<int> out;
    for (const char* name : names) out.push_back(*g.find(name));
    return out;
}

// Проверяет, что быстрый алгоритм, эталон и ожидаемый ответ совпадают.
void expectHammocks(const Graph& g, const std::string& expected) {
    CHECK_EQ(kasyanov(g), expected);
    CHECK_EQ(reference(g), expected);
}

}  // namespace

TEST(hammocks_of_minimal_graph) {
    expectHammocks(makeGraph({"s>t"}), "s>t{s}");
}

TEST(hammocks_of_chain_match_paper_remark) {
    // Замечание из статьи: граф без разветвлений с m = n - 1 содержит n(n - 1)/2 гамаков.
    for (int n = 2; n <= 40; ++n) {
        Graph g;
        for (int i = 0; i < n; ++i) g.addVertex("v" + std::to_string(i));
        for (int i = 0; i + 1 < n; ++i) g.addEdge(i, i + 1);
        hammock::inferEntryExit(g);
        const auto result = hammock::findHammocks(g);
        CHECK_EQ(result.spans.size(), static_cast<std::size_t>(n * (n - 1) / 2));
        CHECK(result.hammocks() == hammock::findHammocksReference(g));
    }
    expectHammocks(makeGraph({"a>b", "b>c", "c>d"}),
                   "a>b{a} a>c{a,b} a>d{a,b,c} b>c{b} b>d{b,c} c>d{c}");
}

TEST(hammocks_of_if_then_else) {
    expectHammocks(makeGraph({"entry>cond", "cond>then", "cond>else", "then>join", "else>join", "join>exit"}),
                   "cond>exit{cond,else,join,then} cond>join{cond,else,then} else>join{else} "
                   "entry>cond{entry} entry>exit{cond,else,entry,join,then} entry>join{cond,else,entry,then} "
                   "join>exit{join} then>join{then}");
}

TEST(hammocks_of_if_without_else) {
    expectHammocks(makeGraph({"s>c", "c>a", "a>j", "c>j", "j>t"}),
                   "a>j{a} c>j{a,c} c>t{a,c,j} j>t{j} s>c{s} s>j{a,c,s} s>t{a,c,j,s}");
}

TEST(hammocks_of_while_loop) {
    // {b} с концом c — не гамак: b ∈ Г(c), т. е. нарушено p ∉ Г(q).
    expectHammocks(makeGraph({"entry>c", "c>b", "b>c", "c>exit"}),
                   "c>exit{b,c} entry>c{entry} entry>exit{b,c,entry}");
}

TEST(hammocks_of_self_loop) {
    expectHammocks(makeGraph({"s>a", "a>a", "a>t"}), "a>t{a} s>a{s} s>t{a,s}");
}

TEST(hammocks_with_end_vertex_having_smaller_m_number) {
    // Гамаки {z} и {y, z} кончаются в заголовке цикла h, у которого M-номер
    // меньше, чем у начала: это ветвь K(U) = {q} леммы 5.
    // {x, y, z} с концом h — не гамак, так как x ∈ Г(h).
    expectHammocks(makeGraph({"entry>h", "h>x", "x>y", "y>z", "z>h", "h>exit"}),
                   "entry>exit{entry,h,x,y,z} entry>h{entry} h>exit{h,x,y,z} x>y{x} x>z{x,y} "
                   "y>h{y,z} y>z{y} z>h{z}");
}

TEST(hammocks_of_irreducible_graph) {
    // Цикл {a, b} с двумя входами — не гамак.
    expectHammocks(makeGraph({"s>a", "s>b", "a>b", "b>a", "a>t", "b>t"}), "s>t{a,b,s}");
}

TEST(hammocks_of_nested_loops) {
    expectHammocks(makeGraph({"entry>a", "a>b", "b>c", "c>b", "c>a", "a>exit"}),
                   "a>exit{a,b,c} entry>a{entry} entry>exit{a,b,c,entry}");
}

TEST(hammocks_of_loop_with_two_exits) {
    // Цикл h -> x -> h с выходами из h и из x в общую вершину j.
    expectHammocks(makeGraph({"s>h", "h>x", "x>h", "h>j", "x>j", "j>t"}),
                   "h>j{h,x} h>t{h,j,x} j>t{j} s>h{s} s>j{h,s,x} s>t{h,j,s,x}");
}

TEST(hammocks_from_graphml_files) {
    const Graph ite = hammock::loadGraphML(kDataDir + "/if_then_else.graphml");
    CHECK_EQ(hammock::findHammocks(ite).spans.size(), static_cast<std::size_t>(8));

    const Graph yed = hammock::loadGraphML(kDataDir + "/yed_loop.graphml");
    expectHammocks(yed, "n0>n1{n0} n0>n3{n0,n1,n2} n1>n3{n1,n2}");

    const Graph nx = hammock::loadGraphML(kDataDir + "/networkx_style.graphml");
    expectHammocks(nx, "1>2{1} 1>4{1,2,3} 1>5{1,2,3,4} 2>4{2,3} 2>5{2,3,4} 4>5{4}");
}

TEST(hammock_members_are_contiguous_in_l_numbering) {
    const Graph g = makeGraph({"entry>h", "h>x", "x>y", "y>z", "z>h", "h>exit"});
    const auto result = hammock::findHammocks(g);
    int previousStartL = 0;
    for (const auto& span : result.spans) {
        const auto members = result.members(span);
        REQUIRE(static_cast<int>(members.size()) == span.size);
        CHECK_EQ(members.front(), span.start);
        for (std::size_t i = 0; i < members.size(); ++i) {
            CHECK_EQ(result.numbering.L[members[i]], result.numbering.L[span.start] + static_cast<int>(i));
        }
        CHECK(result.numbering.L[span.start] >= previousStartL);
        previousStartL = result.numbering.L[span.start];
        CHECK(isHammock(g, members, span.start, span.end));
    }
}

TEST(is_hammock_checks_the_definition) {
    const Graph g = makeGraph({"s>c", "c>a", "c>b", "a>j", "b>j", "j>t"});
    CHECK(isHammock(g, vertices(g, {"c", "a", "b"}), *g.find("c"), *g.find("j")));
    CHECK(isHammock(g, vertices(g, {"a"}), *g.find("a"), *g.find("j")));
    // Две внешних преемника.
    CHECK(!isHammock(g, vertices(g, {"c", "a"}), *g.find("c"), *g.find("j")));
    // Предшественник c вне U, хотя c — не начальная вершина.
    CHECK(!isHammock(g, vertices(g, {"c", "a", "b"}), *g.find("a"), *g.find("j")));
    // Конечная вершина внутри U / начальная вне U / пустое U.
    CHECK(!isHammock(g, vertices(g, {"c", "a", "b", "j"}), *g.find("c"), *g.find("j")));
    CHECK(!isHammock(g, vertices(g, {"a"}), *g.find("b"), *g.find("j")));
    CHECK(!isHammock(g, {}, *g.find("a"), *g.find("j")));
    // Г(U) \ U должно быть равно {q}, а не пусто.
    CHECK(!isHammock(g, vertices(g, {"t"}), *g.find("t"), *g.find("s")));

    const Graph loop = makeGraph({"s>c", "c>b", "b>c", "c>t"});
    CHECK(!isHammock(loop, vertices(loop, {"b"}), *loop.find("b"), *loop.find("c")));  // b ∈ Г(c)
}

TEST(degenerate_sets_from_literal_definition_are_not_hammocks) {
    // {h, exit} с началом h и концом x формально удовлетворяет определению,
    // но содержит выход графа — такие множества не перечисляются.
    const Graph loop = makeGraph({"entry>h", "h>x", "x>y", "y>h", "h>exit"});
    CHECK(isHammock(loop, vertices(loop, {"h", "exit"}), *loop.find("h"), *loop.find("x")));
    expectHammocks(loop, "entry>exit{entry,h,x,y} entry>h{entry} h>exit{h,x,y} x>y{x} y>h{y}");

    // {s, a} формально гамак и с началом a, но началом считается вход s.
    const Graph chain = makeGraph({"s>a", "a>t"});
    CHECK(isHammock(chain, vertices(chain, {"s", "a"}), *chain.find("a"), *chain.find("t")));
    expectHammocks(chain, "a>t{a} s>a{s} s>t{a,s}");
}

TEST(find_hammocks_rejects_non_program_graphs) {
    CHECK_THROWS(hammock::findHammocks(makeGraph({"a>b", "c>b"})), hammock::GraphError);
    CHECK_THROWS(hammock::findHammocksReference(makeGraph({"a>b", "c>b"})), hammock::GraphError);
}
