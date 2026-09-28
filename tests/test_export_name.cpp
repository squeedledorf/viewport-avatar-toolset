#include "check.h"
#include "vats/export_name.h"

using namespace vats;

// T12: spec 03 section 4.2 examples and rules.
TEST(export_file_names) {
    ExportNaming n;
    n.name = "Wave";
    n.side = "Left";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("Wave_01_Left.anim"));
    CHECK_EQ(export_file_name(n, "", true, "anim"), std::string("Wave_01_Right.anim"));
    n.side = "";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("Wave_01.anim"));
    CHECK_EQ(export_file_name(n, "", true, "bvh"), std::string("Wave_01_mirrored.bvh"));
    n.name = "";
    CHECK_EQ(export_file_name(n, "dance", false, "anim"), std::string("dance_01.anim"));
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("Animation_01.anim"));
    n.name = "a/b:c*d?";
    n.number = 123;
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("abcd_123.anim"));
    n.name = "x";
    n.pattern = "__[NAME]--[#]  [SIDE]_-.";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("x-123.anim"));
    n.pattern = "[SIDE]";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("x.anim"));
}

TEST(export_typed_name_is_honoured) {
    ExportNaming n;  // offers "Animation_01.anim" for an untitled project
    CHECK_EQ(typed_export_naming(n, "", false, "Animation_01.anim").pattern, n.pattern);  // the offer: unchanged
    ExportNaming t = typed_export_naming(n, "", false, "wave.anim");
    CHECK(t.name == "wave" && t.pattern == "[NAME]");
    CHECK_EQ(export_file_name(t, "", false, "anim"), std::string("wave.anim"));
    CHECK_EQ(export_file_name(typed_export_naming(n, "", false, "wave"), "", false, "anim"), std::string("wave.anim"));
    n.side = "Left";  // a side the typed name leaves out is dropped
    CHECK_EQ(export_file_name(typed_export_naming(n, "", false, "hold.anim"), "", false, "anim"), std::string("hold.anim"));
    n.side = "";  // mirrored: the suffix is the export's own
    CHECK_EQ(export_file_name(typed_export_naming(n, "", true, "wave_mirrored.anim"), "", true, "anim"),
             std::string("wave_mirrored.anim"));
    CHECK_EQ(typed_export_naming(n, "", false, "").name, n.name);  // nothing typed
}
