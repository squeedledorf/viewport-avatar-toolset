// Soft-body volumes: SL's collision volumes as shells, their share of the flesh, and SL's avatar physics on them.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/shape.h"
#include "vats/dynamics.h"
#include "vats/edit.h"
#include "vats/project.h"
#include "vats/rig_map.h"
#include "vats/skeleton.h"
#include "vats/soft_body.h"

using namespace vats;

namespace {

const AvatarParams& params() {
    static AvatarParams p = [] {
        std::ifstream f(std::string(VATS_DATA_DIR) + "/avatar_lad.xml", std::ios::binary);
        AvatarParams out;
        std::string err;
        parse_avatar_params(std::string{std::istreambuf_iterator<char>(f), {}}, out, err);
        return out;
    }();
    return p;
}

const CollisionVolume& volume(const char* name) { return skel().volumes()[size_t(skel().find_volume(name))]; }

}  // namespace

TEST(soft_body_shell_is_sls_volume_transform) {
    // A flat butt (Butt Size 0 drives Small_Butt to 1: BUTT 8.86 cm wider and 3 cm forward), a hip turned and a pelvis
    // made 10% bigger, and BUTT keyed 1 cm up. SL's world matrix for the volume (LLXformMatrix::updateMatrix): its
    // position (rest, morph and key) scaled by the joint's scale, turned and moved by the joint; its rotation after the
    // joint's; its own scale with the morph, never the joint's.
    BodyShape body = evaluate_shape(skel(), params(), {{795, 0.f}});
    Shape& sh = body.shape;
    const int pelvis = skel().find("mPelvis");
    sh.scale[pelvis] = {1.1, 1.1, 1.1};
    const CollisionVolume& v = volume("BUTT");
    Pose pose(skel().size());
    pose.rot[pelvis] = Quat::axis_angle({0, 1, 0}, 0.4);
    pose.offset[v.node] = {0, 0, 0.01};
    const std::vector<Xform> g = skel().global_pose(pose, &sh);
    const VolumeShell s = volume_shell(g, &sh, v);

    const Xform& joint = g[v.joint];
    const Vec3 local = (v.pos + Vec3{0.03, 0, 0} + Vec3{0, 0, 0.01}).mul({1.1, 1.1, 1.1});
    CHECK((s.frame.pos - (joint.pos + joint.rot.rotate(local))).length() < 1e-9);
    CHECK(std::fabs((s.frame.rot * (joint.rot * v.rot).conj()).normalized().w) > 1 - 1e-9);
    CHECK((s.axes - (v.scale + Vec3{0, 0.0886, 0})).length() < 1e-6);
    // No shape: avatar_skeleton.xml's size.
    CHECK((volume_shell(skel().global_pose(Pose(skel().size())), nullptr, v).axes - v.scale).length() < 1e-12);
}

TEST(soft_body_shell_ray_pick) {
    // An ellipsoid 10 by 20 by 5 cm (semi-axes), turned a quarter about Z and placed at (1, 2, 1).
    const VolumeShell s{{Quat::axis_angle({0, 0, 1}, kPi / 2), {1, 2, 1}}, {0.1, 0.2, 0.05}};
    // Along world X it meets the long axis (the turned local Y): 20 cm before the centre.
    CHECK_NEAR(ray_shell({0, 2, 1}, {1, 0, 0}, s), 0.8, 1e-9);
    // A ray of any length gives t in its own units.
    CHECK_NEAR(ray_shell({0, 2, 1}, {2, 0, 0}, s), 0.4, 1e-9);
    // Along world Y: the short local X, 10 cm.
    CHECK_NEAR(ray_shell({1, 0, 1}, {0, 1, 0}, s), 1.9, 1e-9);
    // Just past the 5 cm half height it misses; pointing away it misses; from inside it hits at once.
    CHECK(ray_shell({0, 2, 1.051}, {1, 0, 0}, s) >= 1e30);
    CHECK(ray_shell({0, 2, 1}, {-1, 0, 0}, s) >= 1e30);
    CHECK(ray_shell({1, 2, 1}, {1, 0, 0}, s) == 0);
}

namespace {

int sk40(const char* name) {
    const int v = skel().find_volume(name);
    return v >= 0 ? dae_volume(skel(), v) : skel().find(name);
}

double weight(const DaeModel& m, size_t v, int j) {
    double w = 0;
    for (size_t k = 0; k < 4; ++k) w += m.joints[v * 4 + k] == j ? m.weights[v * 4 + k] : 0;
    return w;
}

// Butt flesh as a mapped game rig leaves it: cheeks shared between BUTT, the thigh and L_UPPER_LEG (the butt split), a
// bit of pelvis, and a breast's edge shared between LEFT_PEC and mChest.
DaeModel cheeks() {
    DaeModel m;
    m.rigged = true;
    auto vertex = [&](std::initializer_list<std::pair<const char*, float>> w) {
        m.positions.insert(m.positions.end(), {0, 0, 0});
        int k = 0;
        for (const auto& [j, x] : w) m.joints.push_back(sk40(j)), m.weights.push_back(x), ++k;
        for (; k < 4; ++k) m.joints.push_back(dae_root(skel())), m.weights.push_back(0);
    };
    vertex({{"BUTT", 0.5f}, {"mHipLeft", 0.3f}, {"mPelvis", 0.2f}});
    vertex({{"L_UPPER_LEG", 0.4f}, {"BUTT", 0.3f}, {"mHipLeft", 0.2f}, {"mPelvis", 0.1f}});
    vertex({{"BUTT", 0.6f}, {"R_UPPER_LEG", 0.4f}});
    vertex({{"BUTT", 1.0f}});                          // all on BUTT: shares nothing
    vertex({{"mHipLeft", 0.7f}, {"mKneeLeft", 0.3f}});  // the thigh: no BUTT
    vertex({{"LEFT_PEC", 0.6f}, {"mChest", 0.4f}});
    return m;
}

}  // namespace

TEST(soft_body_share_moves_the_shared_flesh_between_volume_and_partner) {
    const DaeModel base = cheeks();
    const int butt = sk40("BUTT");
    // BUTT sits in the middle: its partner is both thighs (with the upper-leg volumes they carry), not the pelvis.
    const std::vector<int> partner = soft_body_partner(skel(), base, butt);
    CHECK(soft_body_partner_name(skel(), partner) == "mHipLeft and mHipRight");
    CHECK(soft_body_partner_name(skel(), soft_body_partner(skel(), base, sk40("LEFT_PEC"))) == "mChest");
    // The pelvis BUTT hangs from shares more of it than the thighs, but the thighs share over half as much: they are
    // the partner that shows on a leg lift. Under half, the pelvis is.
    auto partner_of = [&](float thigh, float pelvis) {  // one vertex each: BUTT with the thigh, BUTT with the pelvis
        DaeModel m;
        const int root = dae_root(skel());
        m.positions.assign(6, 0.f);
        m.joints = {butt, sk40("mHipLeft"), root, root, butt, sk40("mPelvis"), root, root};
        m.weights = {1 - thigh, thigh, 0, 0, 1 - pelvis, pelvis, 0, 0};
        return soft_body_partner_name(skel(), soft_body_partner(skel(), m, butt));
    };
    CHECK(partner_of(0.3f, 0.5f) == "mHipLeft and mHipRight");
    CHECK(partner_of(0.2f, 0.5f) == "mPelvis");
    CHECK(partner_of(0.6f, 0.5f) == "mHipLeft and mHipRight");

    auto shared = [&](double s) {
        DaeModel m = base;
        share_soft_body(skel(), m, butt, s);
        for (size_t v = 0; v < 6; ++v) {  // still normalised, four at most, largest first
            double sum = 0;
            for (size_t k = 0; k < 4; ++k) sum += m.weights[v * 4 + k];
            CHECK_NEAR(sum, 1, 1e-6);
            for (size_t k = 1; k < 4; ++k) CHECK(m.weights[v * 4 + k] <= m.weights[v * 4 + k - 1]);
        }
        CHECK(m.joints.size() == base.joints.size());
        return m;
    };
    const DaeModel none = shared(0), half = shared(0.5), all = shared(1), more = shared(0.75);
    // 0: the shared flesh is all the thigh's; 1: all BUTT's; the pelvis keeps its own either way.
    CHECK_NEAR(weight(none, 0, butt), 0, 1e-6);
    CHECK_NEAR(weight(none, 0, sk40("mHipLeft")), 0.8, 1e-6);
    CHECK_NEAR(weight(all, 0, butt), 0.8, 1e-6);
    CHECK_NEAR(weight(all, 1, butt), 0.9, 1e-6);
    CHECK_NEAR(weight(all, 2, butt), 1, 1e-6);
    CHECK_NEAR(weight(all, 1, sk40("mPelvis")), 0.1, 1e-6);
    CHECK_NEAR(weight(none, 1, sk40("L_UPPER_LEG")) / weight(none, 1, sk40("mHipLeft")), 2, 1e-5);  // kept in proportion
    // 0.5: as loaded; between: part way.
    for (size_t i = 0; i < base.weights.size(); ++i) CHECK(std::fabs(half.weights[i] - base.weights[i]) < 1e-6);
    CHECK(weight(more, 0, butt) > 0.5 && weight(more, 0, butt) < 0.8);
    // Flesh on one of them alone, and other volumes' flesh, stay.
    for (size_t v : {size_t(3), size_t(4), size_t(5)})
        for (size_t k = 0; k < 4; ++k)
            CHECK(none.weights[v * 4 + k] == base.weights[v * 4 + k] && all.weights[v * 4 + k] == base.weights[v * 4 + k]);
}

TEST(soft_body_share_round_trips_through_the_mapping_file) {
    RigMap map;
    map.bones.push_back({"butt_left", "BUTT", 100, "", ""});
    map.share["BUTT"] = 0.2;
    map.share["LEFT_PEC"] = 1;
    RigMap back;
    std::string err;
    CHECK(parse_rig_map_json(write_rig_map_json(map), back, err));
    CHECK(back.share == map.share);
    // As loading applies it: the same weights as the slider gave.
    DaeModel a = cheeks(), b = cheeks();
    for (const auto& [volume, s] : back.share) share_soft_body(skel(), a, sk40(volume.c_str()), s);
    share_soft_body(skel(), b, sk40("BUTT"), 0.2);
    share_soft_body(skel(), b, sk40("LEFT_PEC"), 1);
    CHECK(a.weights == b.weights && a.joints == b.joints);
    CHECK_NEAR(weight(a, 5, sk40("LEFT_PEC")), 1, 1e-6);
    // A volume that is no soft-body volume, or a share that is no number, is left out of what a file says.
    CHECK(parse_rig_map_json(R"({"vats-rig-map": 1, "bones": {}, "share": {"HEAD": 0.3, "BUTT": "x", "BELLY": 7}})", back, err));
    CHECK(back.share.size() == 1 && back.share.at("BELLY") == 1);
}

namespace {

const Rig& rig() {
    static Rig r(skel());
    return r;
}

// A hip drop: the pelvis dips 6 cm by frame 4, is back by frame 8 and holds still to frame 90; the chest turns too.
Clip hip_drop() {
    Clip c;
    c.end_frame = 90;
    c.loop_out = 90;
    key_offset(c, "mPelvis", 0, {});
    key_offset(c, "mPelvis", 4, {0, 0, -0.06});
    key_offset(c, "mPelvis", 8, {});
    key_euler(c, "mChest", 0, {});
    key_euler(c, "mChest", 6, {0, 15, 0});
    key_euler(c, "mHipLeft", 0, {0, -30, 0});
    return c;
}

const AvatarPhysics& natural() {
    for (const auto& [name, p] : avatar_physics_presets())
        if (name == "Natural") return p;
    return avatar_physics_presets().front().second;
}

}  // namespace

TEST(soft_body_avatar_physics_preview_bounces_settles_and_keeps_the_keys) {
    const Clip clip = hip_drop(), before = clip;
    std::vector<DynChain> chains;
    for (const std::string& v : avatar_physics_volumes()) chains.push_back(avatar_physics_chain(v, natural()));
    DynSim sim(skel(), chains);
    const int butt = skel().find("BUTT"), pec = skel().find("LEFT_PEC");
    sim.reset(evaluate(rig(), clip, 0, nullptr).globals);
    double peak_butt = 0, peak_pec = 0, end_butt = 1, end_pec = 1;
    for (int f = 0; f < 90; ++f)
        for (int s = 1; s <= 4; ++s) {  // the preview's real time, a 120 Hz sub-step at a time
            Evaluation e = evaluate(rig(), clip, f + s / 4.0, nullptr);
            sim.step(e.globals, 1.0 / 120);
            Pose p = e.pose;
            sim.apply(e.globals, p);
            const double b = (p.offset[size_t(butt)] - e.pose.offset[size_t(butt)]).length();
            const double c = (p.offset[size_t(pec)] - e.pose.offset[size_t(pec)]).length();
            peak_butt = std::max(peak_butt, b), peak_pec = std::max(peak_pec, c);
            end_butt = b, end_pec = c;
        }
    // The drop throws the butt and the breasts by millimetres to centimetres, as SL's do, then they come to rest.
    CHECK(peak_butt > 0.003 && peak_butt < 0.05);
    CHECK(peak_pec > 0.001 && peak_pec < 0.06);
    CHECK(end_butt < 0.0005 && end_pec < 0.0005);
    CHECK(sim.max_speed() < 0.02);
    CHECK(clip == before);  // a preview: the keys are as they were
    // A Physics wearable as SL makes it (every max effect 0) moves nothing.
    DynSim still(skel(), {avatar_physics_chain("BUTT", AvatarPhysics{})});
    still.reset(evaluate(rig(), clip, 0, nullptr).globals);
    for (int f = 1; f <= 10; ++f) still.step(evaluate(rig(), clip, f, nullptr).globals, 1.0 / 30);
    Pose p(skel().size());
    still.apply(evaluate(rig(), clip, 10, nullptr).globals, p);
    CHECK(p.offset[size_t(butt)].length() == 0);
}

TEST(soft_body_bounce_bakes_only_the_soft_body_volumes) {
    Clip c = hip_drop();
    const Clip before = c;
    CHECK(bake_avatar_physics(c, rig(), nullptr, natural(), avatar_physics_volumes()) == 4);
    for (const auto& [track, channels] : before.curves) CHECK(c.curves.at(track) == channels);  // every other key stays
    for (const auto& [track, channels] : c.curves) {
        if (before.curves.count(track)) continue;
        CHECK(skel().find_volume(track) >= 0 && std::find(avatar_physics_volumes().begin(), avatar_physics_volumes().end(), track) !=
                                                    avatar_physics_volumes().end());
        for (const auto& [ch, curve] : channels) CHECK(ch.rfind("pos_", 0) == 0);
    }
    CHECK(c.curves.count("BUTT") && c.has_channels("BUTT", kPosChannels));
    CHECK(c.dynamics.size() == 4 && c.dynamics[1].physics && c.dynamics[1].baked);
    // Baked again it starts from the keys before the first bake, so the bounce does not add up; unbaked, the keys are back.
    Clip again = c;
    bake_avatar_physics(again, rig(), nullptr, natural(), {"BUTT"});
    CHECK(again.curves == c.curves);
    for (int i = 3; i >= 0; --i) unbake_dynamics(again, skel(), i);
    CHECK(again.curves == before.curves);
    // The settings save with the project.
    Project pr;
    actor_clip(pr, 0) = c;
    Project back;
    std::string err;
    CHECK(load_project(save_project(pr), back, err));
    CHECK(actor_clip(back, 0).dynamics == c.dynamics);
}
