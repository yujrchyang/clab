#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "common/formatter.h"

using namespace TOPNSPC;

// ── Formatter Factory ───────────────────────────────────────────

TEST(FormatterTest, FactoryCreateJson) {
    auto *f = Formatter::create("json");
    ASSERT_NE(f, nullptr);
    delete f;
}

TEST(FormatterTest, FactoryCreateJsonPretty) {
    auto *f = Formatter::create("json-pretty");
    ASSERT_NE(f, nullptr);
    delete f;
}

TEST(FormatterTest, FactoryCreateXml) {
    auto *f = Formatter::create("xml");
    ASSERT_NE(f, nullptr);
    delete f;
}

TEST(FormatterTest, FactoryCreateTable) {
    auto *f = Formatter::create("table");
    ASSERT_NE(f, nullptr);
    delete f;
}

TEST(FormatterTest, FactoryCreateTableKv) {
    auto *f = Formatter::create("table-kv");
    ASSERT_NE(f, nullptr);
    delete f;
}

TEST(FormatterTest, FactoryCreateUnknownReturnsNull) {
    auto *f = Formatter::create("unknown");
    ASSERT_EQ(f, nullptr);
}

// ── JSONFormatter (non-pretty, default p=false) ─────────────────
// The first open_object_section always creates an anonymous root
// brace (names are only printed for nested sections).

TEST(JSONFormatterTest, EmptyObject) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{}");
}

TEST(JSONFormatterTest, SimpleKeyValue) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_string("key", "value");
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"key\":\"value\"}");
}

TEST(JSONFormatterTest, MultipleKeys) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_int("int", 42);
    f.dump_unsigned("uint", 123u);
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"int\":42,\"uint\":123}");
}

TEST(JSONFormatterTest, BoolTrue) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_bool("flag", true);
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"flag\":true}");
}

TEST(JSONFormatterTest, BoolFalse) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_bool("flag", false);
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"flag\":false}");
}

TEST(JSONFormatterTest, Float) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_float("pi", 3.14);
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("\"pi\":"), std::string::npos);
}

TEST(JSONFormatterTest, Null) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_null("empty");
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"empty\":null}");
}

TEST(JSONFormatterTest, JsonArray) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.open_array_section("arr");
    f.dump_int("", 1);
    f.dump_int("", 2);
    f.dump_int("", 3);
    f.close_section();
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"arr\":[1,2,3]}");
}

TEST(JSONFormatterTest, NestedObject) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.open_object_section("child");
    f.dump_string("name", "foo");
    f.close_section();
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"child\":{\"name\":\"foo\"}}");
}

TEST(JSONFormatterTest, StringEscaping) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_string("text", "hello\"world\nline2");
    f.close_section();
    f.flush(os);
    std::string s = os.str();
    EXPECT_NE(s.find("hello\\\"world"), std::string::npos);
    EXPECT_NE(s.find("\\n"), std::string::npos);
}

TEST(JSONFormatterTest, RawJson) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_format_unquoted("raw", "%s", "{\"nested\": true}");
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"raw\":{\"nested\": true}}");
}

TEST(JSONFormatterTest, ResetAndReuse) {
    JSONFormatter f;
    std::ostringstream os1, os2;

    f.open_object_section("");
    f.dump_string("x", "1");
    f.close_section();
    f.flush(os1);
    EXPECT_EQ(os1.str(), "{\"x\":\"1\"}");

    f.reset();
    f.open_object_section("");
    f.dump_string("y", "2");
    f.close_section();
    f.flush(os2);
    EXPECT_EQ(os2.str(), "{\"y\":\"2\"}");
}

TEST(JSONFormatterTest, GetLen) {
    JSONFormatter f;
    f.open_object_section("");
    f.dump_string("k", "v");
    f.close_section();
    EXPECT_GT(f.get_len(), 0);
}

TEST(JSONFormatterTest, NamespacedKey) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.open_object_section_in_ns("item", "http://example.com");
    f.dump_string("name", "test");
    f.close_section();
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("http://example.com"), std::string::npos);
}

TEST(JSONFormatterTest, DumpStream) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    auto &s = f.dump_stream("streamed");
    s << "dynamic content";
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\"streamed\":\"dynamic content\"}");
}

TEST(JSONFormatterTest, DumpObjectWithExternal) {
    struct Obj {
        void dump(Formatter *f) const {
            f->dump_string("field", "value");
        }
    };
    JSONFormatter fmt;
    std::ostringstream os;
    fmt.open_object_section("");
    fmt.dump_object("sub", Obj{});
    fmt.close_section();
    fmt.flush(os);
    EXPECT_EQ(os.str(), "{\"sub\":{\"field\":\"value\"}}");
}

TEST(JSONFormatterTest, IntMaxValues) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_int("min", INT64_MIN);
    f.dump_unsigned("max", UINT64_MAX);
    f.close_section();
    f.flush(os);
    std::string s = os.str();
    EXPECT_NE(s.find(std::to_string(INT64_MIN)), std::string::npos);
    EXPECT_NE(s.find(std::to_string(UINT64_MAX)), std::string::npos);
}

TEST(JSONFormatterTest, NanFloat) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_float("nan", std::numeric_limits<double>::quiet_NaN());
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("null"), std::string::npos);
}

TEST(JSONFormatterTest, InfFloat) {
    JSONFormatter f;
    std::ostringstream os;
    f.open_object_section("");
    f.dump_float("inf", std::numeric_limits<double>::infinity());
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("null"), std::string::npos);
}

// ── JSONFormatter (pretty mode) ─────────────────────────────────

TEST(JSONFormatterTest, PrettyMultiKey) {
    JSONFormatter f(true);
    std::ostringstream os;
    f.open_object_section("");
    f.dump_string("a", "1");
    f.dump_string("b", "2");
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "{\n    \"a\": \"1\",\n    \"b\": \"2\"\n}\n");
}

TEST(JSONFormatterTest, PrettyArray) {
    JSONFormatter f(true);
    std::ostringstream os;
    f.open_object_section("");
    f.open_array_section("items");
    f.dump_int("", 10);
    f.dump_int("", 20);
    f.close_section();
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(),
              "{\n    \"items\": [\n        10,\n        20\n    ]\n}\n");
}

// ── XMLFormatter ────────────────────────────────────────────────

TEST(XMLFormatterTest, EmptyObject) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section("obj");
    f.close_section();
    f.flush(os);
    EXPECT_EQ(os.str(), "<obj></obj>");
}

TEST(XMLFormatterTest, SimpleElement) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section("root");
    f.dump_string("name", "value");
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("<name>value</name>"), std::string::npos);
}

TEST(XMLFormatterTest, PrettyPrint) {
    XMLFormatter f(true);
    std::ostringstream os;
    f.open_object_section("root");
    f.dump_string("x", "1");
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find(">\n"), std::string::npos);
}

TEST(XMLFormatterTest, ElementEscaping) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section("root");
    f.dump_string("data", "a<b&c>d");
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("&lt;"), std::string::npos);
    EXPECT_NE(os.str().find("&amp;"), std::string::npos);
    EXPECT_NE(os.str().find("&gt;"), std::string::npos);
}

TEST(XMLFormatterTest, NullElement) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section("root");
    f.dump_null("empty");
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("xsi:nil=\"true\""), std::string::npos);
}

TEST(XMLFormatterTest, NestedSections) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section("parent");
    f.open_object_section("child");
    f.dump_int("val", 99);
    f.close_section();
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("<child>"), std::string::npos);
    EXPECT_NE(os.str().find("</child>"), std::string::npos);
    EXPECT_NE(os.str().find("<val>99</val>"), std::string::npos);
}

TEST(XMLFormatterTest, HeaderOutput) {
    XMLFormatter f;
    std::ostringstream os;
    f.output_header();
    f.flush(os);
    EXPECT_NE(os.str().find("<?xml"), std::string::npos);
    EXPECT_NE(os.str().find("UTF-8"), std::string::npos);
}

TEST(XMLFormatterTest, Namespace) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section_in_ns("item", "http://ns.example.com");
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("xmlns"), std::string::npos);
}

TEST(XMLFormatterTest, Attrs) {
    XMLFormatter f;
    std::ostringstream os;
    FormatterAttrs attrs("id", "42", NULL);
    f.dump_string_with_attrs("item", "hello", attrs);
    f.flush(os);
    EXPECT_NE(os.str().find("id=\"42\""), std::string::npos);
}

TEST(XMLFormatterTest, DumpStream) {
    XMLFormatter f;
    std::ostringstream os;
    f.open_object_section("root");
    auto &s = f.dump_stream("msg");
    s << "hello";
    f.close_section();
    f.flush(os);
    EXPECT_NE(os.str().find("<msg>hello</msg>"), std::string::npos);
}

// ── TableFormatter ──────────────────────────────────────────────

TEST(TableFormatterTest, SimpleTable) {
    TableFormatter f;
    std::ostringstream os;
    f.dump_string("name", "Alice");
    f.dump_int("age", 30);
    f.flush(os);
    EXPECT_FALSE(os.str().empty());
    EXPECT_NE(os.str().find("Alice"), std::string::npos);
    EXPECT_NE(os.str().find("30"), std::string::npos);
}

TEST(TableFormatterTest, KeyValMode) {
    TableFormatter f(true);
    std::ostringstream os;
    f.dump_string("name", "Bob");
    f.dump_int("age", 25);
    f.flush(os);
    EXPECT_NE(os.str().find("key::name"), std::string::npos);
    EXPECT_NE(os.str().find("key::age"), std::string::npos);
    EXPECT_NE(os.str().find("Bob"), std::string::npos);
}

// ── HTMLFormatter ───────────────────────────────────────────────

TEST(HTMLFormatterTest, BasicOutput) {
    HTMLFormatter f;
    std::ostringstream os;
    f.set_status(200, "OK");
    f.output_header();
    f.dump_string("message", "hello");
    f.flush(os);
    EXPECT_NE(os.str().find("<html>"), std::string::npos);
    EXPECT_NE(os.str().find("200"), std::string::npos);
    EXPECT_NE(os.str().find("OK"), std::string::npos);
    EXPECT_NE(os.str().find("hello"), std::string::npos);
}

TEST(HTMLFormatterTest, NoStatusName) {
    HTMLFormatter f;
    std::ostringstream os;
    f.set_status(404, nullptr);
    f.output_header();
    f.dump_string("error", "not found");
    f.flush(os);
    EXPECT_NE(os.str().find("404"), std::string::npos);
    EXPECT_NE(os.str().find("not found"), std::string::npos);
}

// ── copyable_sstream ────────────────────────────────────────────

TEST(CopyableSstreamTest, CopyConstructor) {
    copyable_sstream s1;
    s1 << "hello";
    copyable_sstream s2(s1);
    EXPECT_EQ(s2.str(), "hello");
}

TEST(CopyableSstreamTest, CopyAssignment) {
    copyable_sstream s1;
    s1 << "world";
    copyable_sstream s2;
    s2 = s1;
    EXPECT_EQ(s2.str(), "world");
}

// ── Fixed String Helpers ────────────────────────────────────────

TEST(FixedStringTest, FixedToString) {
    std::string s = fixed_to_string(12345, 3);
    EXPECT_EQ(s, "12.345");
}

TEST(FixedStringTest, FixedToStringNegative) {
    std::string s = fixed_to_string(-12345, 3);
    EXPECT_EQ(s, "-12.345");
}

TEST(FixedStringTest, FixedUToString) {
    std::string s = fixed_u_to_string(12345, 3);
    EXPECT_EQ(s, "12.345");
}

// ── Stream Escapers ─────────────────────────────────────────────

TEST(XmlStreamEscaperTest, EscapesSpecialChars) {
    std::ostringstream os;
    os << xml_stream_escaper("<a&b>c'd\"e");
    EXPECT_EQ(os.str(), "&lt;a&amp;b&gt;c&apos;d&quot;e");
}

TEST(JsonStreamEscaperTest, EscapesSpecialChars) {
    std::ostringstream os;
    os << json_stream_escaper("a\"b\\c\td\ne");
    EXPECT_EQ(os.str(), "a\\\"b\\\\c\\td\\ne");
}
