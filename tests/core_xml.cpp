// XML: parsing keeps what the canonical writer needs, and the writer reproduces canonical files byte for byte.
#include "notgroot/core/workspace.hpp"
#include "notgroot/core/xml.hpp"
#include "test_support.hpp"
#include <gtest/gtest.h>

using namespace notgroot::core;
namespace fs = std::filesystem;

namespace {
std::string roundTrip(const std::string &text) {
    ParseError error;
    auto root = parseXml(text, error);
    EXPECT_TRUE(root) << error.message << " at line " << error.line;
    return root ? writeXml(*root) : std::string();
}
} // namespace

TEST(Xml, ToyFilesRoundTripExactly) {
    for (const char *name : {"Toy.xml", "lib/Move.xml", "lib/Settle.xml", "lib/node_models.xml"}) {
        std::string text;
        ASSERT_TRUE(readFile(notgroot::test::data("toy") / name, text)) << name;
        EXPECT_EQ(roundTrip(text), text) << name;
    }
}

TEST(Xml, EntitiesAndAttributeOrder) {
    ParseError error;
    auto root = parseXml(R"(<root><A z="1" a="&quot;x&quot; &lt; &amp;" m="line&#10;two"/></root>)", error);
    ASSERT_TRUE(root);
    const auto *a = root->firstChild("A");
    ASSERT_TRUE(a);
    ASSERT_EQ(a->attributes.size(), 3u);
    EXPECT_EQ(a->attributes[0].name, "z");
    EXPECT_EQ(a->get("a"), "\"x\" < &");
    EXPECT_EQ(a->get("m"), "line\ntwo");
    std::string out;
    writeElement(out, *a, 0);
    EXPECT_EQ(out, "<A z=\"1\" a=\"&quot;x&quot; &lt; &amp;\" m=\"line&#10;two\"/>\n");
}

TEST(Xml, ReportsErrorsWithLines) {
    ParseError error;
    EXPECT_FALSE(parseXml("<root>\n<A>\n</root>", error));
    EXPECT_FALSE(error.message.empty());
    EXPECT_GT(error.line, 0);
}

TEST(Xml, EditingKeepsAttributePositions) {
    ParseError error;
    auto root = parseXml(R"(<root><A first="1" second="2" third="3"/></root>)", error);
    auto *a = root->firstChild("A");
    a->set("second", "{key}");
    EXPECT_TRUE(a->rename("third", "fourth"));
    EXPECT_FALSE(a->rename("first", "second"));
    std::string out;
    writeElement(out, *a, 0);
    EXPECT_EQ(out, "<A first=\"1\" second=\"{key}\" fourth=\"3\"/>\n");
}

// The acceptance gate of docs/contract.md: every file of the real trees directory, read and written unchanged, keeps
// its bytes.
TEST(Xml, RealTreesRoundTripExactly) {
    const auto trees = notgroot::test::realTrees();
    if (trees.empty())
        GTEST_SKIP() << "no trees directory (set NOTGROOT_TREES_DIR)";
    int files = 0;
    for (const auto &entry : fs::recursive_directory_iterator(trees)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml" ||
            entry.path().string().find("/.groot/") != std::string::npos)
            continue;
        std::string text;
        ASSERT_TRUE(readFile(entry.path(), text));
        EXPECT_EQ(roundTrip(text), text) << entry.path();
        ++files;
    }
    EXPECT_GT(files, 10);
    std::cout << "round-tripped " << files << " files from " << trees << '\n';
}
