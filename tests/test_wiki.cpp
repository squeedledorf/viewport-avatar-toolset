#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include "check.h"
#include "vats/audio.h"
#include "vats/gif.h"
#include "vats/project.h"
#include "vats/wiki.h"

using namespace vats::wiki;

namespace {

const char* kGraph = R"(# Graph editor

The graph editor shows the **curves** of the selected bones, with *italic* notes and `code`.

> Related articles: [[Keys and timeline]], [[IK#Poles|pole handles]]

## Usage

### Tangent modes

- Auto keeps the curve smooth.
  It continues on a second line.
  - A nested point.
1. First step.
2. Second step with [Second Life Wiki](https://wiki.secondlife.com/wiki/Animation).

| Mode | Effect |
|------|--------|
| Linear | Straight \| lines |
| Stepped | Holds |

> **Note:** Pins show as bands.
> They cover their range.
> **Warning:** Uploading costs L$.

```
vats --screenshot out.png
# not a heading
```

## Troubleshooting

Keys vanish when the loop is off.

Category: Animating
)";

const char* kKeys = R"(# Keys and timeline

Keys hold values at frames. See [[graph editor]] and [[#Usage]].

## Usage

Press **S** to set a key.

Category: Animating
)";

const char* kInstall = R"(# Installation

Unpack the tarball.

Category: Getting started
)";

}  // namespace

TEST(wiki_parse_blocks) {
    Page p = parse_page(kGraph, "graph-editor");
    CHECK_EQ(p.title, std::string("Graph editor"));
    CHECK_EQ(p.category, std::string("Animating"));
    CHECK(p.headings.size() == 3 && p.headings[1] == "Tangent modes");
    int bullets = 0, numbered = 0, tables = 0, code = 0, notes = 0, warnings = 0, related = 0;
    for (const Block& b : p.blocks) {
        bullets += b.kind == Block::Bullet, numbered += b.kind == Block::Numbered, tables += b.kind == Block::Table;
        code += b.kind == Block::Code, notes += b.kind == Block::Note, warnings += b.kind == Block::Warning;
        related += b.kind == Block::Related;
        if (b.kind == Block::Bullet && b.level == 0)
            CHECK(b.spans.size() == 1 && b.spans[0].text == "Auto keeps the curve smooth. It continues on a second line.");
        if (b.kind == Block::Bullet && b.level == 1) CHECK(b.spans[0].text == "A nested point.");
        if (b.kind == Block::Numbered) CHECK(b.number == 1 || b.number == 2);
        if (b.kind == Block::Table) {
            CHECK_EQ(b.rows.size(), size_t(3));  // header + 2, separator dropped
            CHECK(b.rows[1][1][0].text == "Straight | lines");
        }
        if (b.kind == Block::Code) CHECK(b.code == "vats --screenshot out.png\n# not a heading");
        if (b.kind == Block::Note) CHECK(b.spans[0].text == "Pins show as bands. They cover their range.");
        if (b.kind == Block::Related) {
            CHECK(b.spans.size() >= 3);
            CHECK(b.spans[0].link && b.spans[0].target == "Keys and timeline");
            const Span& ik = b.spans.back();
            CHECK(ik.link && ik.target == "IK" && ik.anchor == "Poles" && ik.text == "pole handles");
        }
    }
    CHECK_EQ(bullets, 2);
    CHECK_EQ(numbered, 2);
    CHECK_EQ(tables, 1);
    CHECK_EQ(code, 1);
    CHECK_EQ(notes, 1);
    CHECK_EQ(warnings, 1);
    CHECK_EQ(related, 1);
}

TEST(wiki_inline_runs) {
    auto s = parse_inline("Press **Ctrl+Z** or *undo*; `code` then 2 * 3 and [x](https://a.b/c).");
    CHECK(s[1].bold && s[1].text == "Ctrl+Z");
    CHECK(s[3].italic && s[3].text == "undo");
    CHECK(s[5].code && s[5].text == "code");
    CHECK(s[6].text == " then 2 * 3 and ");  // a spaced asterisk is literal
    CHECK(s[7].link && s[7].external && s[7].target == "https://a.b/c" && s[7].text == "x");
    auto same = parse_inline("[[#Usage]]");
    CHECK(same[0].link && same[0].target.empty() && same[0].anchor == "Usage" && same[0].text == "Usage");
}

TEST(wiki_library_links_contents_search) {
    Library lib;
    lib.add(parse_page(kGraph, "graph-editor"));
    lib.add(parse_page(kKeys, "keys-and-timeline"));
    lib.add(parse_page(kInstall, "installation"));
    CHECK(lib.find("GRAPH EDITOR") && lib.find("graph editor")->file == "graph-editor");
    CHECK(lib.find("keys-and-timeline") && !lib.find("missing"));
    CHECK_EQ(anchor_key("Tangent  modes"), std::string("tangent-modes"));
    CHECK_EQ(anchor_key("IK / FK switch"), std::string("ik-fk-switch"));

    auto contents = lib.contents();
    CHECK(contents.size() == 2 && contents[0].first == "Getting started" && contents[1].first == "Animating");
    CHECK(contents[1].second[0]->title == "Graph editor" && contents[1].second[1]->title == "Keys and timeline");

    auto hits = lib.search("graph");
    CHECK(!hits.empty() && hits[0].page->title == "Graph editor");  // the title match ranks first
    hits = lib.search("loop keys");  // every word must appear
    CHECK(hits.size() == 1 && hits[0].page->file == "graph-editor" && hits[0].heading == "Troubleshooting");
    CHECK(hits[0].snippet.find("loop") != std::string::npos);
    CHECK(lib.search("zebra").empty() && lib.search("  ").empty());
}

TEST(wiki_load_dir_skips_style) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "vats_wiki_test";
    fs::create_directories(dir);
    std::ofstream(dir / "installation.md") << kInstall;
    std::ofstream(dir / "STYLE.md") << "# Style\n";
    std::ofstream(dir / "notes.txt") << "x";
    Library lib;
    CHECK_EQ(lib.load_dir(dir.string()), 1);
    CHECK(lib.find("Installation") != nullptr);
    fs::remove_all(dir);
}

TEST(wiki_brackets_in_code_are_not_links) {
    Page p = parse_page("# Format\n\nA curve is `[[0, 0, 2]]` in the file.\n\n```\n\"rot_z\": [[0, 0, 2], [15, 40, 2]]\n```\n", "format");
    CHECK_EQ(p.blocks.size(), size_t(2));
    for (const Span& s : p.blocks[0].spans) CHECK(!s.link);
    CHECK(p.blocks[0].spans[1].code && p.blocks[0].spans[1].text == "[[0, 0, 2]]");
    CHECK(p.blocks[1].kind == Block::Code && p.blocks[1].code == "\"rot_z\": [[0, 0, 2], [15, 40, 2]]");
}

// The shipped pages: every link and section resolves, every page has one of the nine categories.
TEST(wiki_shipped_pages) {
    Library lib;
    CHECK(lib.load_dir(VATS_WIKI_DIR) >= 40);
    const std::vector<std::string> nine = {"Getting started", "Interface", "Animating", "Motion", "Import and export",
                                           "Second Life", "Viewer", "Reference", "Troubleshooting"};
    auto contents = lib.contents();
    CHECK_EQ(contents.size(), nine.size());
    for (size_t i = 0; i < contents.size() && i < nine.size(); ++i) CHECK_EQ(contents[i].first, nine[i]);
    auto has_heading = [](const Page& p, const std::string& anchor) {
        for (auto& h : p.headings)
            if (anchor_key(h) == anchor_key(anchor)) return true;
        return false;
    };
    int links = 0;
    for (const Page& p : lib.pages()) {
        CHECK(!p.title.empty() && !p.category.empty());
        auto check = [&](const std::vector<Span>& spans) {
            for (const Span& s : spans) {
                if (!s.link || s.external || s.example) continue;
                ++links;
                const Page* to = s.target.empty() ? &p : lib.find(s.target);
                if (!to) std::fprintf(stderr, "  %s: no page \"%s\"\n", p.file.c_str(), s.target.c_str());
                else if (!s.anchor.empty() && !has_heading(*to, s.anchor))
                    std::fprintf(stderr, "  %s: no section \"%s#%s\"\n", p.file.c_str(), s.target.c_str(), s.anchor.c_str());
                CHECK(to && (s.anchor.empty() || has_heading(*to, s.anchor)));
            }
        };
        for (const Block& b : p.blocks) {
            check(b.spans);
            for (auto& row : b.rows)
                for (auto& cell : row) check(cell);
        }
    }
    CHECK(links > 100);
    for (const char* word : {"firewall", "priority", "iPhone"}) CHECK(!lib.search(word).empty());
}

TEST(wiki_images_and_examples_parse) {
    Page p = parse_page("# Graph editor\n\nText.\n![The Graph panel](images/graph-editor/panel.png)\n*The elbow at frame 22.*\n"
                        "![No caption](images/graph-editor/b.png)\n\nNot a caption.\n\n"
                        "Try it: [Open the example](example:graph-basics.vat).\n\n"
                        "[Open the example](example:wave.vat) [Show the target](target:wave-done.vat)\n",
                        "graph-editor");
    CHECK_EQ(p.blocks.size(), size_t(6));
    CHECK(p.blocks[0].kind == Block::Paragraph);
    const Block& a = p.blocks[1];
    CHECK(a.kind == Block::Image && a.alt == "The Graph panel" && a.image == "images/graph-editor/panel.png");
    CHECK(a.spans.size() == 1 && a.spans[0].italic && a.spans[0].text == "The elbow at frame 22.");
    CHECK(p.blocks[2].kind == Block::Image && p.blocks[2].spans.empty() && p.blocks[2].image == "images/graph-editor/b.png");
    CHECK(p.blocks[3].kind == Block::Paragraph);
    CHECK(p.blocks[4].spans.size() == 3);
    const Span& ex = p.blocks[4].spans[1];
    CHECK(ex.link && ex.example && !ex.ghost && !ex.external && ex.target == "graph-basics.vat" && ex.text == "Open the example");
    // A target link is an example link (a button, its file checked by wiki_examples) that shows it as the target ghost.
    const auto& pair = p.blocks[5].spans;
    CHECK_EQ(pair.size(), size_t(3));
    CHECK(pair[0].example && !pair[0].ghost && pair[0].target == "wave.vat");
    CHECK(pair[2].link && pair[2].example && pair[2].ghost && !pair[2].external && pair[2].target == "wave-done.vat" &&
          pair[2].text == "Show the target");
    CHECK(p.plain.find("the graph panel") != std::string::npos);  // alt text is searchable
    int w = 0, h = 0;
    CHECK(!png_size(std::string(VATS_WIKI_DIR) + "/vats.md", w, h));
}

namespace {

// Every image and example link of the shipped pages, with the page it is on.
struct Refs {
    std::vector<std::pair<const Page*, const Block*>> images;
    std::vector<std::pair<const Page*, std::string>> examples;  // example and target links
    int targets = 0;
};
Refs shipped_refs(const Library& lib) {
    Refs r;
    for (const Page& p : lib.pages())
        for (const Block& b : p.blocks) {
            if (b.kind == Block::Image) r.images.emplace_back(&p, &b);
            auto scan = [&](const std::vector<Span>& spans) {
                for (const Span& s : spans)
                    if (s.example) r.examples.emplace_back(&p, s.target), r.targets += s.ghost;
            };
            scan(b.spans);
            for (auto& row : b.rows)
                for (auto& cell : row) scan(cell);
        }
    return r;
}

}  // namespace

// STYLE.md, Images: each referenced image is in images/<page>/, has alt text, and is a PNG at most 1200 px wide and
// 250 KB, or a GIF that decodes, at most 640 px wide and 450 KB; every file in images/ (but the shot manifest) is
// referenced.
TEST(wiki_images) {
    namespace fs = std::filesystem;
    Library lib;
    lib.load_dir(VATS_WIKI_DIR);
    const fs::path wiki = VATS_WIKI_DIR;
    Refs refs = shipped_refs(lib);
    CHECK(!refs.images.empty());
    std::set<fs::path> used;
    for (auto& [page, b] : refs.images) {
        const fs::path file = wiki / b->image;
        int w = 0, h = 0;
        std::error_code ec;
        const fs::path ext = fs::path(b->image).extension();
        bool fits = false;
        if (ext == ".png") {
            fits = png_size(file.string(), w, h) && w <= 1200 && fs::file_size(file, ec) <= 250 * 1024;
        } else if (ext == ".gif" && gif_size(file.string(), w, h) && w <= 640 && fs::file_size(file, ec) <= 450 * 1024) {
            std::ifstream f(file, std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            vats::GifImage gif;
            fits = vats::read_gif(bytes.data(), bytes.size(), gif) && gif.width == w && gif.height == h;
        }
        const bool ok = fits && !b->alt.empty() && b->image.rfind("images/" + page->file + "/", 0) == 0 &&
                        b->image.find("..") == std::string::npos;
        if (!ok)
            std::fprintf(stderr, "  %s: image %s is missing, misplaced, without alt text, or not a PNG (<= 1200 px, 250 KB) "
                                 "or GIF (<= 640 px, 450 KB) that reads\n",
                         page->file.c_str(), b->image.c_str());
        CHECK(ok);
        used.insert(file.lexically_normal());
    }
    for (const auto& e : fs::recursive_directory_iterator(wiki / "images")) {
        if (!e.is_regular_file() || e.path().filename() == "shots.json") continue;
        const bool referenced = used.count(e.path().lexically_normal()) > 0;
        if (!referenced) std::fprintf(stderr, "  no page shows %s\n", e.path().string().c_str());
        CHECK(referenced);
    }
}

// STYLE.md, Worked examples: every example link and every target link has its file in examples/, and it loads.
TEST(wiki_examples) {
    namespace fs = std::filesystem;
    Library lib;
    lib.load_dir(VATS_WIKI_DIR);
    Refs refs = shipped_refs(lib);
    CHECK(!refs.examples.empty() && refs.targets > 0);
    for (auto& [page, file] : refs.examples) {
        std::ifstream f(fs::path(VATS_WIKI_DIR) / "examples" / file, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        vats::Project proj;
        std::string err;
        const bool ok = f && file.find_first_of("/\\") == std::string::npos && vats::load_project(ss.str(), proj, err, file) &&
                        !proj.clip.curves.empty();
        // Its audio, if it names a file, is beside it in examples/ and decodes (the help ships it).
        vats::AudioData audio;
        const bool audio_ok = !ok || !proj.clip.audio || proj.clip.audio->path.empty() ||
                              (proj.clip.audio->path.find_first_of("/\\") == std::string::npos &&
                               vats::load_audio_file((fs::path(VATS_WIKI_DIR) / "examples" / proj.clip.audio->path).string(), audio, err));
        if (!audio_ok) std::fprintf(stderr, "  %s: the audio %s of %s is missing or does not decode: %s\n", page->file.c_str(),
                                    proj.clip.audio->path.c_str(), file.c_str(), err.c_str());
        CHECK(audio_ok);
        if (!ok) std::fprintf(stderr, "  %s: example or target %s is missing or does not load: %s\n", page->file.c_str(), file.c_str(), err.c_str());
        CHECK(ok);
    }
}

// The whole shipped help folder (what CMake installs and tools/sync-to-viewer.sh copies) stays within 24 MB.
TEST(wiki_budget) {
    namespace fs = std::filesystem;
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(VATS_WIKI_DIR)) {
        const std::string ext = e.path().extension().string();
        if (e.is_regular_file() && e.path().filename() != "STYLE.md" && (ext == ".md" || ext == ".png" || ext == ".gif" || ext == ".vat" || ext == ".wav"))
            total += e.file_size();
    }
    if (total > 24u * 1024 * 1024) std::fprintf(stderr, "  the help folder is %ju bytes, over 24 MB\n", total);
    CHECK(total <= 24u * 1024 * 1024);
}

// Help text layout, with every character one unit wide.
TEST(wiki_layout) {
    auto measure = [](const Span&, std::string_view t) { return float(t.size()); };
    auto text_of = [](const std::vector<Span>& spans, const Layout& lay) {
        std::string out;
        for (auto& pc : lay.pieces) out += spans[pc.span].text.substr(pc.begin, pc.end - pc.begin) + "|";
        return out;
    };
    const float pad = 0.5f;
    // Code at the start of a line, of several words, then wrapping: one background per line of it, inside the
    // width, padding its text on both sides, and never under the text of other spans.
    for (float w : {40.f, 30.f, 12.f, 7.f}) {
        const std::vector<Span> spans = parse_inline("`663 / 250,000 bytes`: the file is 663 bytes, `a b`.");
        const Layout lay = lay_out(spans, w, pad, 3, 1, measure);
        for (auto& pc : lay.pieces) {
            CHECK(pc.x >= 0 && pc.x + pc.width <= w);
            bool boxed = false;
            for (auto& b : lay.boxes)
                if (b.line == pc.line && pc.x >= b.x0 + pad && pc.x + pc.width + pad <= b.x1) boxed = true;
                else if (b.line == pc.line && !spans[pc.span].code) CHECK(pc.x >= b.x1 || pc.x + pc.width <= b.x0);
            CHECK(boxed == spans[pc.span].code);
        }
        for (auto& b : lay.boxes) CHECK(b.x0 >= 0 && b.x1 <= w);
        std::vector<int> per_line(size_t(lay.lines));
        for (auto& b : lay.boxes) ++per_line[size_t(b.line)];
        for (int n : per_line) CHECK(n <= 2);
        if (w == 40) {
            CHECK_EQ(lay.boxes.size(), size_t(2));
            CHECK_EQ(lay.boxes[0].x0, 0.f);
            CHECK_EQ(lay.pieces[0].x, pad);
            CHECK_EQ(text_of(spans, lay), std::string("663| /| 250,000| bytes|:| the| file| is| 663|bytes,| |a| b|.|"));
        }
    }
    // A word wider than the line breaks after a path separator, else anywhere; nothing is lost.
    const std::vector<Span> path = parse_inline("See `~/Projects/very-long-folder-name/and_more/file.anim` now");
    const Layout lay = lay_out(path, 16, pad, 3, 1, measure);
    std::string joined;
    for (auto& pc : lay.pieces) {
        CHECK(pc.x + pc.width <= 16);
        if (path[pc.span].code) joined += path[pc.span].text.substr(pc.begin, pc.end - pc.begin);
    }
    CHECK_EQ(joined, path[1].text);
    CHECK_EQ(text_of(path, lay), std::string("See| |~/Projects/|very-long-|folder-name/|and_more/file.|anim| now|"));
    // Punctuation right after code stays on the line with it.
    const std::vector<Span> comma = parse_inline("xxxx `abc`, y");
    const Layout c = lay_out(comma, 9, pad, 3, 1, measure);
    CHECK(c.pieces.size() == 5 && c.pieces[2].line == 1 && c.pieces[3].line == 1 && c.pieces[2].x == pad);  // xxxx, " ", abc, ",", " y"
    const std::vector<Span> paren = parse_inline("ab (**cd**) e");
    const Layout pl = lay_out(paren, 6, pad, 3, 1, measure);
    CHECK(pl.pieces.size() == 5 && pl.pieces[1].line == 1 && pl.pieces[2].line == 1 && pl.pieces[3].line == 1);  // ab, (, cd, ), e
    // An example link is one button, never broken.
    Span ex;
    ex.text = "Open the example", ex.link = ex.example = true;
    const Layout b = lay_out({ex}, 8, pad, 3, 1, measure);
    CHECK_EQ(b.pieces.size(), size_t(1));
    CHECK_EQ(b.pieces[0].width, 22.f);
}

// Table columns share a narrow width without any going under its widest word; a labelled link keeps its cell.
TEST(wiki_table_columns) {
    CHECK(column_widths({10, 20}, {5, 5}, 100) == std::vector<float>({10, 20}));
    const std::vector<float> w = column_widths({300, 20, 100}, {40, 20, 30}, 200);
    CHECK_NEAR(w[0] + w[1] + w[2], 200, 1e-3);
    CHECK(w[0] >= 40 && w[1] == 20 && w[2] >= 30 && w[0] > w[2]);
    const std::vector<float> tight = column_widths({300, 300}, {150, 250}, 200);
    CHECK_NEAR(tight[0] + tight[1], 200, 1e-3);
    const std::vector<float> path = column_widths({60, 400}, {50, 380}, 200);  // a short label beside a long path
    CHECK(path[0] == 50 && path[1] == 150);
    Page p = parse_page("# T\n\n| a | b |\n|---|---|\n| `x` | see [[Page#Part|the part]] |\n", "t");
    CHECK(p.blocks.size() == 1 && p.blocks[0].rows[1].size() == 2 && p.blocks[0].rows[1][1][1].text == "the part");
}

// A page's "Order: <n>" line places it within its category; pages without one follow, by title.
TEST(wiki_contents_follow_order_lines) {
    Library lib;
    lib.add(parse_page("# Zebra\n\nText.\n\nCategory: Getting started\nOrder: 1\n", "zebra"));
    lib.add(parse_page("# Apple\n\nText.\n\nCategory: Getting started\n", "apple"));
    lib.add(parse_page("# Mango\n\nOrder: 2 is text here.\n\nCategory: Getting started\nOrder: 2\n", "mango"));
    const auto contents = lib.contents();
    CHECK(lib.find("zebra")->order == 1 && lib.find("apple")->order == 0 && lib.find("mango")->order == 2);
    CHECK(contents.size() == 1 && contents[0].second.size() == 3);
    CHECK(contents[0].second[0]->file == "zebra" && contents[0].second[1]->file == "mango" &&
          contents[0].second[2]->file == "apple");
    CHECK(lib.find("mango")->plain.find("is text here") != std::string::npos);  // a sentence starting "Order:" stays text
}

// The shipped tutorials appear in the help's contents in the order tutorials.md lists them.
TEST(wiki_tutorials_in_list_order) {
    Library lib;
    lib.load_dir(VATS_WIKI_DIR);
    const Page* index = lib.find("tutorials");
    CHECK(index != nullptr);
    if (!index) return;
    std::vector<std::string> listed;
    for (const Block& b : index->blocks)
        if (b.kind == Block::Bullet || b.kind == Block::Numbered)
            for (const Span& s : b.spans)
                if (s.link && !s.external) {
                    listed.push_back(s.target);
                    break;
                }
    CHECK(listed.size() >= 19);
    std::vector<std::string> shown;
    const auto contents = lib.contents();
    for (auto& [cat, pages] : contents)
        if (cat == index->category)
            for (const Page* p : pages)
                if (std::find(listed.begin(), listed.end(), p->title) != listed.end()) shown.push_back(p->title);
    CHECK(shown == listed);
    CHECK_EQ(contents[0].second[0]->title, std::string("VATs"));
}
