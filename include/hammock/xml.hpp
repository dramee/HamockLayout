#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Небольшой самодостаточный XML-парсер, достаточный для чтения GraphML.
// Поддерживает: XML-декларацию и инструкции обработки, комментарии, CDATA,
// DOCTYPE (пропускается), предопределённые и числовые сущности,
// пространства имён (xmlns / xmlns:prefix). Не поддерживает сущности,
// объявленные в DTD.
namespace hammock::xml {

class XmlError : public std::runtime_error {
public:
    XmlError(const std::string& message, int line)
        : std::runtime_error("XML, строка " + std::to_string(line) + ": " + message), line_(line) {}
    int line() const { return line_; }

private:
    int line_;
};

struct Attribute {
    std::string ns;         // URI пространства имён (пусто для атрибутов без префикса)
    std::string localName;  // имя без префикса
    std::string qName;      // имя как в документе
    std::string value;      // значение с раскрытыми сущностями
};

struct Element {
    std::string ns;         // URI пространства имён элемента
    std::string localName;
    std::string qName;
    std::vector<Attribute> attributes;  // без объявлений xmlns
    std::vector<std::unique_ptr<Element>> children;
    std::string text;       // символьные данные, лежащие непосредственно в элементе
    int line = 0;

    // Значение атрибута без пространства имён с указанным локальным именем.
    const std::string* attribute(std::string_view localName) const;
};

std::unique_ptr<Element> parse(std::string_view document);

}  // namespace hammock::xml
