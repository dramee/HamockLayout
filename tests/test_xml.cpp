#include "hammock/xml.hpp"
#include "test_framework.hpp"

using hammock::xml::parse;
using hammock::xml::XmlError;

TEST(xml_parses_elements_attributes_and_text) {
    const auto root = parse("<?xml version='1.0'?>\n<a x=\"1\" y='two'><b/>text<c>inner</c></a>");
    CHECK_EQ(root->localName, std::string("a"));
    REQUIRE(root->attribute("x") != nullptr);
    CHECK_EQ(*root->attribute("x"), std::string("1"));
    CHECK_EQ(*root->attribute("y"), std::string("two"));
    CHECK(root->attribute("z") == nullptr);
    REQUIRE(root->children.size() == 2);
    CHECK_EQ(root->children[0]->localName, std::string("b"));
    CHECK_EQ(root->children[1]->text, std::string("inner"));
    CHECK_EQ(root->text, std::string("text"));
    CHECK_EQ(root->line, 2);
}

TEST(xml_decodes_entities_and_character_references) {
    const auto root = parse("<a v=\"&lt;&amp;&gt;&quot;&apos;\">&#65;&#x42;&#1046;&#x1F600;</a>");
    CHECK_EQ(*root->attribute("v"), std::string("<&>\"'"));
    CHECK_EQ(root->text, std::string("AB\xD0\x96\xF0\x9F\x98\x80"));
}

TEST(xml_normalizes_whitespace_in_attributes) {
    const auto root = parse("<a v=\"x\ty\nz\"/>");
    CHECK_EQ(*root->attribute("v"), std::string("x y z"));
}

TEST(xml_skips_comments_pi_doctype_and_reads_cdata) {
    const auto root = parse(
        "\xEF\xBB\xBF<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE a [ <!ELEMENT a ANY> <!ATTLIST a v CDATA \"]>\"> ]>\n"
        "<!-- comment <a> -->\n"
        "<a><!-- inside --><?pi data?><![CDATA[<raw & text>]]></a>\n"
        "<!-- trailing -->\n");
    CHECK_EQ(root->localName, std::string("a"));
    CHECK_EQ(root->text, std::string("<raw & text>"));
    CHECK(root->children.empty());
}

TEST(xml_resolves_namespaces) {
    const auto root = parse(
        "<g:root xmlns:g=\"urn:g\" xmlns=\"urn:default\">"
        "  <child y:attr=\"1\" plain=\"2\" xmlns:y=\"urn:y\"/>"
        "  <inner xmlns=\"\"><bare/></inner>"
        "</g:root>");
    CHECK_EQ(root->ns, std::string("urn:g"));
    CHECK_EQ(root->localName, std::string("root"));
    CHECK_EQ(root->qName, std::string("g:root"));
    CHECK(root->attributes.empty());  // объявления xmlns не попадают в атрибуты
    REQUIRE(root->children.size() == 2);
    const auto& child = *root->children[0];
    CHECK_EQ(child.ns, std::string("urn:default"));
    REQUIRE(child.attributes.size() == 2);
    CHECK_EQ(child.attributes[0].ns, std::string("urn:y"));
    CHECK_EQ(child.attributes[0].localName, std::string("attr"));
    CHECK(child.attribute("attr") == nullptr);  // атрибут с префиксом не находится по локальному имени
    CHECK_EQ(*child.attribute("plain"), std::string("2"));
    CHECK_EQ(root->children[1]->ns, std::string(""));
    CHECK_EQ(root->children[1]->children[0]->ns, std::string(""));
}

TEST(xml_reports_errors) {
    CHECK_THROWS(parse(""), XmlError);
    CHECK_THROWS(parse("just text"), XmlError);
    CHECK_THROWS(parse("<a>"), XmlError);
    CHECK_THROWS(parse("<a></b>"), XmlError);
    CHECK_THROWS(parse("<a><b></a></b>"), XmlError);
    CHECK_THROWS(parse("<a x=1/>"), XmlError);
    CHECK_THROWS(parse("<a x='1' x='2'/>"), XmlError);
    CHECK_THROWS(parse("<a x='1'y='2'/>"), XmlError);
    CHECK_THROWS(parse("<a x='<'/>"), XmlError);
    CHECK_THROWS(parse("<a>&unknown;</a>"), XmlError);
    CHECK_THROWS(parse("<a>&#xD800;</a>"), XmlError);
    CHECK_THROWS(parse("<a>&#xZZ;</a>"), XmlError);
    CHECK_THROWS(parse("<p:a/>"), XmlError);
    CHECK_THROWS(parse("<a/><b/>"), XmlError);
    CHECK_THROWS(parse("<a><!-- unclosed </a>"), XmlError);
    CHECK_THROWS(parse("<a><![CDATA[ unclosed </a>"), XmlError);
}

TEST(xml_error_contains_line_number) {
    try {
        parse("<a>\n<b>\n</c>\n</a>");
        CHECK(false);
    } catch (const XmlError& e) {
        CHECK_EQ(e.line(), 3);
    }
}
