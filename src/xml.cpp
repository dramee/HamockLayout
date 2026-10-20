#include "hammock/xml.hpp"

#include <cstdint>
#include <map>

namespace hammock::xml {

const std::string* Element::attribute(std::string_view name) const {
    for (const auto& a : attributes) {
        if (a.ns.empty() && a.localName == name) return &a.value;
    }
    return nullptr;
}

namespace {

constexpr std::string_view kXmlNamespace = "http://www.w3.org/XML/1998/namespace";

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool isNameStart(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || u >= 0x80;
}

bool isNameChar(char c) {
    return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::unique_ptr<Element> parseDocument() {
        if (startsWith("\xEF\xBB\xBF")) pos_ += 3;  // UTF-8 BOM
        skipMisc(true);
        if (eof() || peek() != '<') fail("ожидался корневой элемент");
        auto root = parseElement();
        skipMisc(false);
        if (!eof()) fail("лишние данные после корневого элемента");
        return root;
    }

private:
    using NsScope = std::map<std::string, std::string>;  // префикс -> URI ("" — пространство по умолчанию)

    std::string_view s_;
    std::size_t pos_ = 0;
    int line_ = 1;
    std::vector<NsScope> scopes_;

    [[noreturn]] void fail(const std::string& msg) const { throw XmlError(msg, line_); }

    bool eof() const { return pos_ >= s_.size(); }
    char peek() const { return s_[pos_]; }
    bool startsWith(std::string_view p) const { return s_.substr(pos_, p.size()) == p; }

    void advance(std::size_t count = 1) {
        for (std::size_t i = 0; i < count && pos_ < s_.size(); ++i, ++pos_) {
            if (s_[pos_] == '\n') ++line_;
        }
    }

    bool skipSpaces() {
        const std::size_t start = pos_;
        while (!eof() && isSpace(peek())) advance();
        return pos_ != start;
    }

    void expect(char c) {
        if (eof() || peek() != c) fail(std::string("ожидался символ '") + c + "'");
        advance();
    }

    // Пропускает всё до terminator включительно.
    void skipUntil(std::string_view terminator, const char* what) {
        const std::size_t end = s_.find(terminator, pos_);
        if (end == std::string_view::npos) fail(std::string("незакрытый ") + what);
        advance(end + terminator.size() - pos_);
    }

    // Пролог/эпилог документа: пробелы, комментарии, инструкции обработки, DOCTYPE.
    void skipMisc(bool allowDoctype) {
        for (;;) {
            skipSpaces();
            if (startsWith("<?")) {
                skipUntil("?>", "инструкция обработки");
            } else if (startsWith("<!--")) {
                skipUntil("-->", "комментарий");
            } else if (allowDoctype && startsWith("<!DOCTYPE")) {
                skipDoctype();
            } else {
                return;
            }
        }
    }

    void skipDoctype() {
        int depth = 0;
        while (!eof()) {
            const char c = peek();
            if (c == '"' || c == '\'') {
                advance();
                while (!eof() && peek() != c) advance();
            } else if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            } else if (c == '>' && depth == 0) {
                advance();
                return;
            }
            advance();
        }
        fail("незакрытый DOCTYPE");
    }

    std::string parseName() {
        if (eof() || !isNameStart(peek())) fail("ожидалось имя");
        const std::size_t start = pos_;
        while (!eof() && isNameChar(peek())) advance();
        return std::string(s_.substr(start, pos_ - start));
    }

    // Разбирает ссылку на сущность, начиная с '&', и дописывает её значение в out.
    void parseReference(std::string& out) {
        expect('&');
        const std::size_t end = s_.find(';', pos_);
        if (end == std::string_view::npos || end - pos_ > 16) fail("некорректная ссылка на сущность");
        const std::string_view name = s_.substr(pos_, end - pos_);
        advance(end + 1 - pos_);
        if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "amp") out += '&';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x';
            const std::string_view digits = name.substr(hex ? 2 : 1);
            if (digits.empty()) fail("пустая числовая ссылка");
            std::uint32_t cp = 0;
            for (const char c : digits) {
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else fail("некорректная числовая ссылка &" + std::string(name) + ";");
                cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(d);
                if (cp > 0x10FFFF) fail("код символа вне диапазона Unicode");
            }
            if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) fail("недопустимый код символа");
            appendUtf8(out, cp);
        } else {
            fail("неизвестная сущность &" + std::string(name) + ";");
        }
    }

    std::string parseAttributeValue() {
        if (eof() || (peek() != '"' && peek() != '\'')) fail("значение атрибута должно быть в кавычках");
        const char quote = peek();
        advance();
        std::string value;
        for (;;) {
            if (eof()) fail("незакрытое значение атрибута");
            const char c = peek();
            if (c == quote) {
                advance();
                return value;
            }
            if (c == '<') fail("символ '<' в значении атрибута");
            if (c == '&') {
                parseReference(value);
            } else {
                value += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
                advance();
            }
        }
    }

    std::string resolvePrefix(const std::string& prefix) const {
        if (prefix == "xml") return std::string(kXmlNamespace);
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(prefix);
            if (found != it->end()) return found->second;
        }
        if (prefix.empty()) return {};
        fail("необъявленный префикс пространства имён '" + prefix + "'");
    }

    static void splitQName(const std::string& qName, std::string& prefix, std::string& local) {
        const std::size_t colon = qName.find(':');
        if (colon == std::string::npos) {
            prefix.clear();
            local = qName;
        } else {
            prefix = qName.substr(0, colon);
            local = qName.substr(colon + 1);
        }
    }

    std::unique_ptr<Element> parseElement() {
        auto element = std::make_unique<Element>();
        element->line = line_;
        expect('<');
        element->qName = parseName();

        // Атрибуты в исходном виде; пространства имён разрешаются после чтения всех атрибутов.
        std::vector<std::pair<std::string, std::string>> raw;
        NsScope scope;
        for (;;) {
            const bool hadSpace = skipSpaces();
            if (eof()) fail("незакрытый тег <" + element->qName + ">");
            if (peek() == '/' || peek() == '>') break;
            if (!hadSpace) fail("между атрибутами нужен пробел");
            std::string name = parseName();
            skipSpaces();
            expect('=');
            skipSpaces();
            std::string value = parseAttributeValue();
            for (const auto& r : raw) {
                if (r.first == name) fail("повторный атрибут '" + name + "'");
            }
            if (name == "xmlns") scope[""] = value;
            else if (name.rfind("xmlns:", 0) == 0) scope[name.substr(6)] = value;
            raw.emplace_back(std::move(name), std::move(value));
        }
        scopes_.push_back(std::move(scope));

        std::string prefix, local;
        splitQName(element->qName, prefix, local);
        element->localName = local;
        element->ns = resolvePrefix(prefix);
        for (auto& [name, value] : raw) {
            if (name == "xmlns" || name.rfind("xmlns:", 0) == 0) continue;
            Attribute a;
            splitQName(name, prefix, local);
            a.qName = name;
            a.localName = local;
            a.ns = prefix.empty() ? std::string() : resolvePrefix(prefix);
            a.value = std::move(value);
            element->attributes.push_back(std::move(a));
        }

        if (startsWith("/>")) {
            advance(2);
            scopes_.pop_back();
            return element;
        }
        expect('>');
        parseContent(*element);
        scopes_.pop_back();
        return element;
    }

    void parseContent(Element& element) {
        for (;;) {
            if (eof()) fail("незакрытый элемент <" + element.qName + "> (строка " + std::to_string(element.line) + ")");
            if (startsWith("</")) {
                advance(2);
                const std::string name = parseName();
                if (name != element.qName) {
                    fail("закрывающий тег </" + name + "> не соответствует <" + element.qName + ">");
                }
                skipSpaces();
                expect('>');
                return;
            }
            if (startsWith("<!--")) {
                skipUntil("-->", "комментарий");
            } else if (startsWith("<![CDATA[")) {
                advance(9);
                const std::size_t end = s_.find("]]>", pos_);
                if (end == std::string_view::npos) fail("незакрытая секция CDATA");
                element.text.append(s_.substr(pos_, end - pos_));
                advance(end + 3 - pos_);
            } else if (startsWith("<?")) {
                skipUntil("?>", "инструкция обработки");
            } else if (startsWith("<!")) {
                fail("неожиданная конструкция '<!'");
            } else if (peek() == '<') {
                element.children.push_back(parseElement());
            } else if (peek() == '&') {
                parseReference(element.text);
            } else {
                element.text += peek();
                advance();
            }
        }
    }
};

}  // namespace

std::unique_ptr<Element> parse(std::string_view document) {
    return Parser(document).parseDocument();
}

}  // namespace hammock::xml
