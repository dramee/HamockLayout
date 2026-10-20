#include "hammock/graphml.hpp"

#include "hammock/xml.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace hammock {

namespace {

[[noreturn]] void fail(const std::string& msg, int line) {
    throw GraphMLError("GraphML, строка " + std::to_string(line) + ": " + msg);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool isGraphML(const xml::Element& e) { return e.ns.empty() || e.ns == kGraphMLNamespace; }

bool is(const xml::Element& e, std::string_view name) { return isGraphML(e) && e.localName == name; }

const std::string& requireAttribute(const xml::Element& e, std::string_view name) {
    const std::string* value = e.attribute(name);
    if (value == nullptr) fail("у элемента <" + e.localName + "> нет атрибута '" + std::string(name) + "'", e.line);
    return *value;
}

bool isTrue(const std::string& value) {
    const std::string v = lower(trim(value));
    return v == "true" || v == "1" || v == "yes";
}

// Роль вершины, интересная для поиска гамаков.
enum class Role { None, Entry, Exit };

// Ключи GraphML (<key>), относящиеся к вершинам и описывающие вход/выход.
struct RoleKeys {
    enum class Kind { RoleName, EntryFlag, ExitFlag };
    struct Key {
        Kind kind;
        std::string defaultValue;
        bool hasDefault = false;
    };
    std::map<std::string, Key> byId;

    Role roleFrom(Kind kind, const std::string& value) const {
        switch (kind) {
            case Kind::RoleName: {
                const std::string v = lower(trim(value));
                if (v == "entry" || v == "start" || v == "source") return Role::Entry;
                if (v == "exit" || v == "end" || v == "sink") return Role::Exit;
                return Role::None;
            }
            case Kind::EntryFlag: return isTrue(value) ? Role::Entry : Role::None;
            case Kind::ExitFlag: return isTrue(value) ? Role::Exit : Role::None;
        }
        return Role::None;
    }
};

RoleKeys collectRoleKeys(const xml::Element& root) {
    RoleKeys keys;
    for (const auto& child : root.children) {
        if (!is(*child, "key")) continue;
        const std::string& id = requireAttribute(*child, "id");
        const std::string* domain = child->attribute("for");
        if (domain != nullptr && *domain != "node" && *domain != "all") continue;
        const std::string* attrName = child->attribute("attr.name");
        if (attrName == nullptr) continue;
        const std::string name = lower(trim(*attrName));
        RoleKeys::Key key;
        if (name == "role") key.kind = RoleKeys::Kind::RoleName;
        else if (name == "entry") key.kind = RoleKeys::Kind::EntryFlag;
        else if (name == "exit") key.kind = RoleKeys::Kind::ExitFlag;
        else continue;
        for (const auto& d : child->children) {
            if (is(*d, "default")) {
                key.defaultValue = d->text;
                key.hasDefault = true;
            }
        }
        keys.byId[id] = std::move(key);
    }
    return keys;
}

Role nodeRole(const xml::Element& node, const RoleKeys& keys) {
    Role role = Role::None;
    auto merge = [&](Role r) {
        if (r == Role::None) return;
        if (role != Role::None && role != r) fail("вершина помечена одновременно как вход и выход", node.line);
        role = r;
    };
    std::map<std::string, bool> seen;
    for (const auto& d : node.children) {
        if (!is(*d, "data")) continue;
        const std::string& keyId = requireAttribute(*d, "key");
        const auto it = keys.byId.find(keyId);
        if (it == keys.byId.end()) continue;
        seen[keyId] = true;
        merge(keys.roleFrom(it->second.kind, d->text));
    }
    for (const auto& [id, key] : keys.byId) {
        if (key.hasDefault && !seen.count(id)) merge(keys.roleFrom(key.kind, key.defaultValue));
    }
    return role;
}

struct PendingEdge {
    std::string source;
    std::string target;
    int line;
};

}  // namespace

Graph parseGraphML(std::string_view document, const GraphMLOptions& options) {
    const auto root = xml::parse(document);
    if (!is(*root, "graphml")) fail("корневой элемент должен быть <graphml>", root->line);

    const RoleKeys keys = collectRoleKeys(*root);

    const xml::Element* graphElement = nullptr;
    for (const auto& child : root->children) {
        if (is(*child, "graph")) {
            graphElement = child.get();
            break;
        }
    }
    if (graphElement == nullptr) fail("в документе нет элемента <graph>", root->line);

    const std::string* edgeDefault = graphElement->attribute("edgedefault");
    const bool directedByDefault = edgeDefault == nullptr || *edgeDefault == "directed";
    if (edgeDefault != nullptr && *edgeDefault != "directed" && *edgeDefault != "undirected") {
        fail("некорректное значение edgedefault='" + *edgeDefault + "'", graphElement->line);
    }

    Graph g;
    int entry = -1;
    int exit = -1;
    std::vector<PendingEdge> edges;
    for (const auto& child : graphElement->children) {
        const xml::Element& e = *child;
        if (!isGraphML(e)) continue;
        if (e.localName == "node") {
            const std::string& id = requireAttribute(e, "id");
            for (const auto& sub : e.children) {
                if (is(*sub, "graph")) fail("вложенные графы не поддерживаются (вершина '" + id + "')", sub->line);
            }
            if (g.find(id)) fail("вершина '" + id + "' объявлена повторно", e.line);
            const int v = g.addVertex(id);
            const Role role = nodeRole(e, keys);
            if (role == Role::Entry) {
                if (entry >= 0) fail("несколько вершин помечены как вход", e.line);
                entry = v;
            } else if (role == Role::Exit) {
                if (exit >= 0) fail("несколько вершин помечены как выход", e.line);
                exit = v;
            }
        } else if (e.localName == "edge") {
            const std::string* directed = e.attribute("directed");
            const bool isDirected = directed == nullptr ? directedByDefault : *directed == "true";
            if (directed != nullptr && *directed != "true" && *directed != "false") {
                fail("некорректное значение directed='" + *directed + "'", e.line);
            }
            if (!isDirected) fail("неориентированные дуги не поддерживаются: гамаки определены для ориентированных графов", e.line);
            edges.push_back({requireAttribute(e, "source"), requireAttribute(e, "target"), e.line});
        } else if (e.localName == "hyperedge") {
            fail("гипердуги не поддерживаются", e.line);
        }
    }

    // В GraphML дуги могут ссылаться на вершины, объявленные позже, поэтому
    // дуги добавляются после чтения всех вершин.
    for (const auto& edge : edges) {
        const auto s = g.find(edge.source);
        const auto t = g.find(edge.target);
        if (!s) fail("дуга ссылается на необъявленную вершину '" + edge.source + "'", edge.line);
        if (!t) fail("дуга ссылается на необъявленную вершину '" + edge.target + "'", edge.line);
        g.addEdge(*s, *t);
    }

    auto byId = [&](const std::string& id, const char* what) {
        const auto v = g.find(id);
        if (!v) throw GraphMLError(std::string(what) + ": вершина '" + id + "' не найдена в графе");
        return *v;
    };
    if (!options.entryId.empty()) entry = byId(options.entryId, "вход");
    if (!options.exitId.empty()) exit = byId(options.exitId, "выход");
    if (entry >= 0) g.setEntry(entry);
    if (exit >= 0) g.setExit(exit);
    inferEntryExit(g);
    return g;
}

Graph loadGraphML(const std::string& path, const GraphMLOptions& options) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw GraphMLError("не удалось открыть файл '" + path + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseGraphML(buffer.str(), options);
}

namespace {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

std::string writeGraphML(const Graph& g) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<graphml xmlns=\"" << kGraphMLNamespace << "\">\n"
        << "  <key id=\"role\" for=\"node\" attr.name=\"role\" attr.type=\"string\"/>\n"
        << "  <graph id=\"G\" edgedefault=\"directed\">\n";
    for (int v = 0; v < g.vertexCount(); ++v) {
        out << "    <node id=\"" << escape(g.id(v)) << "\"";
        if (v == g.entry()) out << "><data key=\"role\">entry</data></node>\n";
        else if (v == g.exit()) out << "><data key=\"role\">exit</data></node>\n";
        else out << "/>\n";
    }
    for (int v = 0; v < g.vertexCount(); ++v) {
        for (const int w : g.successors(v)) {
            out << "    <edge source=\"" << escape(g.id(v)) << "\" target=\"" << escape(g.id(w)) << "\"/>\n";
        }
    }
    out << "  </graph>\n</graphml>\n";
    return out.str();
}

}  // namespace hammock
