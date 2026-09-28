// The animated GIF writer (spec 08 LM): the file walks as GIF89a with the right frame count and delays, and every
// frame's LZW data decodes back to the picture (through its own palette). And the reader the help's GIFs go through.
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "vats/gif.h"
#include "vats/wiki.h"

using namespace vats;

namespace {

struct Decoded {
    int width = 0, height = 0, frames = 0;
    bool loops = false, ok = false;
    std::vector<int> delays;
    std::vector<std::vector<std::uint8_t>> rgb;  // each frame, 3 bytes a pixel
};

// A reference LZW decoder, written from the GIF89a definition.
bool unlzw(const std::vector<std::uint8_t>& data, int min_size, size_t pixels, std::vector<int>& out) {
    const int clear = 1 << min_size, end = clear + 1;
    std::vector<std::vector<int>> table;
    auto reset = [&] {
        table.assign(size_t(clear + 2), {});
        for (int i = 0; i < clear; ++i) table[size_t(i)] = {i};
    };
    reset();
    int size = min_size + 1, prev = -1;
    size_t bit = 0;
    while (bit + size_t(size) <= data.size() * 8) {
        int code = 0;
        for (int b = 0; b < size; ++b, ++bit) code |= ((data[bit / 8] >> (bit % 8)) & 1) << b;
        if (code == clear) {
            reset(), size = min_size + 1, prev = -1;
            continue;
        }
        if (code == end) return out.size() == pixels;
        std::vector<int> entry;
        if (code < int(table.size())) entry = table[size_t(code)];
        else if (code == int(table.size()) && prev >= 0) entry = table[size_t(prev)], entry.push_back(table[size_t(prev)][0]);
        else return false;
        out.insert(out.end(), entry.begin(), entry.end());
        if (prev >= 0 && table.size() < 4096) {
            std::vector<int> add = table[size_t(prev)];
            add.push_back(entry[0]);
            table.push_back(add);
            if (int(table.size()) == (1 << size) && size < 12) ++size;
        }
        prev = code;
    }
    return false;
}

Decoded decode(const std::vector<std::uint8_t>& f) {
    Decoded d;
    size_t p = 0;
    auto u8 = [&] { return p < f.size() ? f[p++] : 0; };
    auto u16 = [&] { int lo = u8(); return lo | u8() << 8; };
    auto sub_blocks = [&] {
        std::vector<std::uint8_t> data;
        for (int n = u8(); n > 0 && p + size_t(n) <= f.size(); n = u8()) data.insert(data.end(), f.begin() + long(p), f.begin() + long(p + n)), p += size_t(n);
        return data;
    };
    if (f.size() < 13 || std::string(f.begin(), f.begin() + 6) != "GIF89a") return d;
    p = 6;
    d.width = u16(), d.height = u16();
    const int flags = u8();
    u8(), u8();
    if (flags & 0x80) p += size_t(3) << ((flags & 7) + 1);
    while (p < f.size()) {
        const int b = u8();
        if (b == 0x3B) return d.ok = true, d;
        if (b == 0x21) {
            const int label = u8();
            const std::vector<std::uint8_t> data = sub_blocks();
            if (label == 0xF9 && data.size() == 4) d.delays.push_back(data[1] | data[2] << 8);
            if (label == 0xFF && data.size() >= 11 && std::string(data.begin(), data.begin() + 11) == "NETSCAPE2.0") d.loops = true;
        } else if (b == 0x2C) {
            u16(), u16();
            const int w = u16(), h = u16(), lf = u8();
            std::vector<std::uint8_t> palette;
            if (lf & 0x80) {
                const size_t n = size_t(3) << ((lf & 7) + 1);
                palette.assign(f.begin() + long(p), f.begin() + long(std::min(p + n, f.size())));
                p += n;
            }
            const int min_size = u8();
            std::vector<int> idx;
            if (!unlzw(sub_blocks(), min_size, size_t(w) * h, idx)) return d;
            std::vector<std::uint8_t> rgb;
            for (int i : idx)
                for (int c = 0; c < 3; ++c) rgb.push_back(size_t(i) * 3 + 2 < palette.size() ? palette[size_t(i) * 3 + size_t(c)] : 0);
            d.rgb.push_back(std::move(rgb));
            ++d.frames;
        } else {
            return d;
        }
    }
    return d;
}

}  // namespace

TEST(gif_frames_decode) {
    const int w = 211, h = 97;  // odd sizes, and enough pixels for the code table to fill and clear
    GifWriter g(w, h);
    std::vector<std::vector<std::uint8_t>> pictures;
    for (int f = 0; f < 3; ++f) {
        std::vector<std::uint8_t> rgba(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t* q = &rgba[(size_t(y) * w + x) * 4];
                // A few flat colours with noise-like variety in one band, so the palette must choose.
                q[0] = std::uint8_t(x * 255 / (w - 1)), q[1] = std::uint8_t((y * 37 + f * 80) & 0xF8), q[2] = std::uint8_t((x ^ y) * 3);
                q[3] = 255;
            }
        g.add_frame(rgba.data(), gif_delay_cs(f, 30));
        pictures.push_back(rgba);
    }
    CHECK_EQ(g.frames(), 3);
    const std::vector<std::uint8_t> file = g.finish();
    const Decoded d = decode(file);
    CHECK(d.ok);
    CHECK_EQ(d.width, w);
    CHECK_EQ(d.height, h);
    CHECK_EQ(d.frames, 3);
    CHECK(d.loops);
    CHECK(d.delays == std::vector<int>({3, 4, 3}));
    // Each pixel comes back within the quantiser's reach (5 bits a channel, then the box's mean).
    int worst = 0;
    for (int f = 0; f < d.frames; ++f)
        for (size_t i = 0; i < size_t(w) * h; ++i)
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(int(d.rgb[size_t(f)][i * 3 + size_t(c)]) - int(pictures[size_t(f)][i * 4 + size_t(c)])));
    CHECK(worst <= 48);

    // A flat picture: a tiny palette, exact colours.
    GifWriter flat(16, 16);
    std::vector<std::uint8_t> grey(16 * 16 * 4, 128);
    flat.add_frame(grey.data(), 10);
    const Decoded e = decode(flat.finish());
    CHECK(e.ok && e.frames == 1 && e.rgb[0][0] == 128 && e.rgb[0].back() == 128);
}

TEST(gif_delays_keep_time) {
    int total = 0;
    for (int i = 0; i < 30; ++i) total += gif_delay_cs(i, 30);
    CHECK_EQ(total, 100);
    CHECK_EQ(gif_delay_cs(0, 25), 4);
    CHECK_EQ(gif_delay_cs(0, 10), 10);
}

// The reader (stb_image) on a GIF from another encoder: every frame, composited, with the delays as the file says.
TEST(gif_reads_fixture) {
    const std::string path = std::string(VATS_TEST_FILES) + "/help-tiny.gif";
    std::ifstream f(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    GifImage g;
    CHECK(read_gif(bytes.data(), bytes.size(), g));
    CHECK_EQ(g.width, 4);
    CHECK_EQ(g.height, 3);
    CHECK_EQ(g.frames(), 3);
    CHECK(g.delays_ms == std::vector<int>({70, 0, 250}));
    CHECK_EQ(g.rgba.size(), size_t(3 * 4 * 3 * 4));
    for (int i = 0; i < g.frames() && g.rgba.size() == 144; ++i) {  // red, green, blue, opaque, first and last pixel
        const std::uint8_t* first = &g.rgba[size_t(i) * 48];
        const std::uint8_t* last = first + 44;
        for (int c = 0; c < 4; ++c) CHECK(first[c] == (c == i || c == 3 ? 255 : 0) && last[c] == first[c]);
    }
    int w = 0, h = 0;
    CHECK(wiki::gif_size(path, w, h) && w == 4 && h == 3);

    // Our own writer's file reads back with its delays (hundredths of a second, as milliseconds).
    GifWriter writer(8, 8);
    std::vector<std::uint8_t> px(8 * 8 * 4, 200);
    writer.add_frame(px.data(), 7);
    writer.add_frame(px.data(), 12);
    const std::vector<std::uint8_t> ours = writer.finish();
    CHECK(read_gif(ours.data(), ours.size(), g) && g.frames() == 2 && g.delays_ms == std::vector<int>({70, 120}));

    // Cut short, empty, not a GIF.
    CHECK(!read_gif(bytes.data(), 20, g) && g.frames() == 0);
    CHECK(!read_gif(nullptr, 0, g));
    const std::uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    CHECK(!read_gif(png, sizeof png, g));
    CHECK(!wiki::gif_size(std::string(VATS_WIKI_DIR) + "/vats.md", w, h));
}

// A thumbnail that drew nothing (every pixel's alpha 0, whatever its colour) is blank; one opaque pixel is not, nor is
// anything that is not a readable PNG (a GIF, cut short, nothing).
TEST(png_blank_finds_empty_thumbnails) {
    auto bytes = [](const char* name) {
        std::ifstream f(std::string(VATS_TEST_FILES) + "/" + name, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    const std::vector<std::uint8_t> blank = bytes("thumb-blank.png"), drawn = bytes("thumb-drawn.png"), gif = bytes("help-tiny.gif");
    CHECK(blank.size() > 8 && drawn.size() > 8);
    CHECK(png_blank(blank.data(), blank.size()));
    CHECK(!png_blank(drawn.data(), drawn.size()));
    CHECK(!png_blank(gif.data(), gif.size()));
    CHECK(!png_blank(blank.data(), 20));
    CHECK(!png_blank(nullptr, 0));
}
