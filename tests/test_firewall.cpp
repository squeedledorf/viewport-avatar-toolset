#include <cstdio>
#include <fstream>
#include <map>

#include "check.h"
#include "firewall.h"

using namespace vats;

namespace {

// Answers commands from a table and records what was asked; nothing real runs.
struct FakeRunner {
    std::map<std::string, std::pair<int, std::string>> answers;
    std::vector<std::string> asked;
    CommandRunner fn() {
        return [this](const std::string& cmd, std::string& out) {
            asked.push_back(cmd);
            auto it = answers.find(cmd);
            if (it == answers.end()) return out.clear(), 127;
            out = it->second.second;
            return it->second.first;
        };
    }
};

std::string temp_conf(const char* text) {
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/vats_ufw_test.conf";
    std::ofstream(path) << text;
    return path;
}

}  // namespace

TEST(firewall_subnet_and_interfaces) {
    CHECK_EQ(subnet_of("192.168.68.77", 22), std::string("192.168.68.0/22"));
    CHECK_EQ(subnet_of("10.1.2.3", 8), std::string("10.0.0.0/8"));
    CHECK_EQ(subnet_of("192.168.1.20", 32), std::string("192.168.1.20/32"));
    CHECK_EQ(subnet_of("192.168.1.300", 24), std::string());
    CHECK_EQ(subnet_of("not an ip", 24), std::string());
    CHECK_EQ(subnet_of("192.168.1.2", 33), std::string());
    CHECK(is_virtual_interface("docker0") && is_virtual_interface("wg0-mullvad") && is_virtual_interface("lo"));
    CHECK(!is_virtual_interface("enp39s0") && !is_virtual_interface("wlan0"));
    for (auto& a : lan_addresses()) CHECK(!a.subnet.empty() && !is_virtual_interface(a.name));
}

#if defined(__linux__)
TEST(firewall_detection_without_root) {
    if (std::getenv("VATS_FAKE_FIREWALL")) return;  // an override is meant to win
    FakeRunner r;
    CHECK(detect_firewall(r.fn(), "/nonexistent") == Firewall::Off);  // neither tool present

    r.answers["systemctl is-active ufw"] = {0, "active\n"};
    CHECK(detect_firewall(r.fn(), temp_conf("# ufw\nENABLED=yes\n")) == Firewall::Ufw);
    CHECK(detect_firewall(r.fn(), temp_conf("ENABLED=no\n")) == Firewall::Off);  // service up, filtering off
    CHECK(detect_firewall(r.fn(), "/nonexistent") == Firewall::Ufw);              // unreadable: trust the service

    FakeRunner f;
    f.answers["systemctl is-active ufw"] = {3, "inactive\n"};
    f.answers["firewall-cmd --state"] = {0, "running\n"};
    CHECK(detect_firewall(f.fn(), "/nonexistent") == Firewall::Firewalld);
    for (auto& cmd : f.asked) CHECK(cmd.find("sudo") == std::string::npos && cmd.find("pkexec") == std::string::npos);
    std::remove(temp_conf("").c_str());
}
#endif

TEST(firewall_allow_commands) {
    CHECK_EQ(allow_command(Firewall::Ufw, 49983, "192.168.68.0/22", "pkexec"),
             std::string("pkexec ufw allow from 192.168.68.0/22 to any port 49983 proto udp"));
    CHECK_EQ(allow_command(Firewall::Ufw, 39539, "10.0.0.0/8", "sudo"),
             std::string("sudo ufw allow from 10.0.0.0/8 to any port 39539 proto udp"));
    const std::string fw = allow_command(Firewall::Firewalld, 14043, "192.168.1.0/24", "pkexec");
    CHECK(fw.rfind("pkexec sh -c '", 0) == 0);
    CHECK(fw.find("source address=192.168.1.0/24 port port=14043 protocol=udp accept") != std::string::npos);
    CHECK(fw.find("&& firewall-cmd --reload'") != std::string::npos);
    CHECK_EQ(allow_command(Firewall::Off, 49983, "192.168.1.0/24", "pkexec"), std::string());
    CHECK_EQ(allow_command(Firewall::Unknown, 49983, "192.168.1.0/24", "pkexec"), std::string());
    CHECK_EQ(allow_command(Firewall::Ufw, 49983, "", "pkexec"), std::string());  // no network to allow
    CHECK_EQ(allow_command(Firewall::Ufw, 0, "192.168.1.0/24", "pkexec"), std::string());
}
