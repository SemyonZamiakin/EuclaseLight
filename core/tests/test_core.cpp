#include "ql/effects.h"
#include "ql/guard.h"
#include "ql/metrics_wire.h"
#include "ql/monitor_link.h"
#include "../third_party/corundum/corundum.h"
#include <cmath>
#include "ql/openrgb.h"
#include "ql/qlcore.h"
#include "ql/sysinfo.h"
#include <cstdio>
#include <cstring>
#include <map>

static int g_run = 0, g_fail = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// Simulated SMBus: a few devices, records every raw write that reaches "hardware".
struct SimBus : ql::IBus {
    std::map<uint8_t, bool> present;
    std::vector<std::pair<uint8_t, uint8_t>> rawWrites;
    int rawProbes = 0;
    ql::BusInfo info() const override { return {"sim:0", "Simulated SMBus", ql::BusKind::SMBus}; }
    bool probe(uint8_t a) override { ++rawProbes; return present.count(a) != 0; }
    bool readByte(uint8_t a, uint8_t, uint8_t& o) override { o = 0x42; return present.count(a) != 0; }
    bool writeByte(uint8_t a, uint8_t r, uint8_t) override { rawWrites.push_back({a, r}); return true; }
    bool writeBlock(uint8_t a, uint8_t r, const uint8_t*, size_t) override { rawWrites.push_back({a, r}); return true; }
};

static void test_guard() {
    SimBus sim;
    sim.present = {{0x50, true}, {0x52, true}, {0x19, true}, {0x71, true}, {0x2F, true}, {0x03, true}};
    ql::GuardedBus bus(sim);

    auto hits = ql::scanBus(bus);
    CHECK(hits.size() == 5);                 // 0x03 is reserved: never probed, never reported
    CHECK(sim.rawProbes == 0x77 - 0x08 + 1);  // exactly the legal range, nothing else
    int prot = 0;
    for (auto& h : hits) prot += h.protectedRange;
    CHECK(prot == 3);                         // 0x50, 0x52, 0x19

    CHECK(!bus.writeByte(0x50, 0, 1));        // SPD: always denied
    CHECK(!bus.grantWrite(0x50));             // ...and cannot be granted
    CHECK(!bus.grantWrite(0x03));
    CHECK(!bus.writeByte(0x71, 0, 1));        // no grant yet
    CHECK(bus.grantWrite(0x71));
    CHECK(bus.writeByte(0x71, 0x10, 7));
    uint8_t blk[40] = {};
    CHECK(!bus.writeBlock(0x71, 0, blk, 40)); // oversize block
    CHECK(bus.writeBlock(0x71, 0, blk, 8));
    CHECK(sim.rawWrites.size() == 2);         // only the two allowed writes hit the bus
    CHECK(sim.rawWrites[0].first == 0x71);
    bus.revokeAll();
    CHECK(!bus.writeByte(0x71, 0, 1));
    CHECK(!bus.audit().empty());
}

static void test_effects() {
    ql::EffectParams p;
    ql::Rgb a[16], b[16];
    p.type = ql::EffectType::Static; p.color1 = {10, 20, 30}; p.brightness = 1;
    ql::renderEffect(p, 1.0, a, 16);
    CHECK(a[7].r == 10 && a[7].g == 20 && a[7].b == 30);
    p.brightness = 0;
    ql::renderEffect(p, 1.0, a, 16);
    CHECK(a[0].r == 0 && a[15].b == 0);
    for (int t = 0; t < int(ql::EffectType::Count); ++t) {
        p.type = ql::EffectType(t); p.brightness = 1;
        ql::renderEffect(p, 3.25, a, 16);
        ql::renderEffect(p, 3.25, b, 16);
        CHECK(memcmp(a, b, sizeof a) == 0);   // deterministic
        ql::renderEffect(p, 0.0, nullptr, 16); // null-safe
    }
    p.type = ql::EffectType::RainbowWave;
    ql::renderEffect(p, 0, a, 16);
    CHECK(a[0].r == 255 && a[0].g == 0);      // hue 0 = red at the start
    CHECK(!(a[0].r == a[8].r && a[0].g == a[8].g && a[0].b == a[8].b));
}

static void test_smbios() {
    // Hand-built: Type 17 (Memory Device), 1 string-less + strings table.
    uint8_t t[0x22 + 64] = {};
    t[0] = 17; t[1] = 0x22;
    t[0x0C] = 0x00; t[0x0D] = 0x40;           // 16384 MB
    t[0x10] = 1; t[0x12] = 0x1A;              // locator #1, DDR4
    t[0x15] = 0xB8; t[0x16] = 0x0B;           // 3000
    t[0x17] = 2; t[0x1A] = 3;                 // manufacturer #2, part #3
    size_t o = 0x22;
    const char* strs[] = {"DIMM_A1", "Kingston", "KHX3000C15/16GX"};
    for (auto s : strs) { memcpy(t + o, s, strlen(s) + 1); o += strlen(s) + 1; }
    t[o++] = 0;
    uint8_t end[6] = {127, 4, 0, 0, 0, 0};
    memcpy(t + o, end, 6);
    ql::SystemInfo si;
    ql::parseSmbios(t, o + 6, si);
    CHECK(si.ram.size() == 1);
    if (si.ram.size() == 1) {
        CHECK(si.ram[0].capacityBytes == 16384ull * 1048576ull);
        CHECK(si.ram[0].type == "DDR4" && si.ram[0].speedMTs == 3000);
        CHECK(si.ram[0].manufacturer == "Kingston" && si.ram[0].partNumber == "KHX3000C15/16GX");
        CHECK(si.ram[0].slot == "DIMM_A1");
    }
    // garbage must not crash
    uint8_t junk[5] = {17, 0xFF, 1, 2, 3};
    ql::SystemInfo s2;
    ql::parseSmbios(junk, sizeof junk, s2);
    ql::parseSmbios(nullptr, 0, s2);
    CHECK(s2.ram.empty());
}

static void test_capi() {
    CHECK(ql_abi_version() == 2);
    CHECK(ql_effect_count() == int(ql::EffectType::Count));
    const char* j = ql_system_json();
    CHECK(j && j[0] == '{' && strstr(j, "\"gpus\"") && strstr(j, "\"ram\""));
    uint8_t px[3 * 4];
    ql_render(0, 0.5f, 1.f, 0xFF0000, 0, 0, 0.0, px, 4);
    CHECK(px[0] == 255 && px[1] == 0);
    ql_render(99, 0.5f, 1.f, 0, 0, 0, 0.0, px, 4);  // bad type ignored
    printf("detected: %s\n", j);
}

static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }
static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
static void putStr(std::vector<uint8_t>& v, const char* s) { put16(v, uint16_t(strlen(s) + 1)); v.insert(v.end(), s, s + strlen(s) + 1); }

static void test_openrgb() {
    // Build a protocol-3 device payload by hand and parse it back.
    ql::OrgbMode direct; direct.name = "Direct"; direct.flags = 0x130; direct.colorMode = 1; direct.brightMax = 100; direct.brightness = 99;
    ql::OrgbMode breath = direct; breath.name = "Breathing"; breath.flags = 0x131; breath.speedMax = 10; breath.speed = 5;
    std::vector<uint8_t> d;
    put32(d, 0);               // total size (ignored by parser)
    put32(d, 2);               // GPU
    putStr(d, "Test GPU"); putStr(d, "Acme"); putStr(d, "desc"); putStr(d, "v1"); putStr(d, "sn"); putStr(d, "loc");
    put16(d, 2); put32(d, 0);  // 2 modes, active 0
    for (const auto* m : {&direct, &breath}) { auto b = ql::serializeOrgbMode(*m, 3); d.insert(d.end(), b.begin(), b.end()); }
    put16(d, 1);               // 1 zone
    putStr(d, "Zone"); put32(d, 1); put32(d, 1); put32(d, 1); put32(d, 1); put16(d, 0);
    put16(d, 1); putStr(d, "LED 0"); put32(d, 0);
    put16(d, 1); put32(d, 0);  // 1 colour
    ql::OrgbDevice dev;
    CHECK(ql::parseOrgbDevice(d.data(), d.size(), dev, 3));
    CHECK(dev.name == "Test GPU" && dev.vendor == "Acme" && dev.type == 2);
    CHECK(dev.modes.size() == 2 && dev.modes[1].name == "Breathing" && dev.modes[1].speed == 5);
    CHECK(dev.ledCount == 1);

    // every truncation must fail cleanly, never read out of bounds
    for (size_t cut = 0; cut < d.size(); ++cut) { ql::OrgbDevice t; ql::parseOrgbDevice(d.data(), cut, t, 3); }
    CHECK(!ql::parseOrgbDevice(d.data(), d.size() - 3, dev, 3));

    bool approx = true;
    CHECK(ql::chooseOrgbMode(dev, ql::EffectType::Breathing, approx) == 1 && !approx);
    CHECK(ql::chooseOrgbMode(dev, ql::EffectType::Static, approx) == 0 && !approx);
    CHECK(ql::chooseOrgbMode(dev, ql::EffectType::Comet, approx) == 0 && approx);  // no comet: static stand-in

    ql::OpenRgbClient c;
    CHECK(!c.connect("127.0.0.1", 1, 300));  // nothing listens on port 1
    CHECK(!c.connected() && !c.lastError().empty());
    CHECK(ql_orgb_connect(0) == -1);
}

// Builds an EMS1 snapshot the way Euclase Monitor does (numbers and one string).
static std::vector<uint8_t> snapshot(uint64_t seq, uint64_t ms, double cpu, double gpu, double temp) {
    std::vector<uint8_t> v = {'E', 'M', 'S', '1'};
    put32(v, 4);
    for (int i = 0; i < 8; ++i) v.push_back(uint8_t(seq >> (8 * i)));
    for (int i = 0; i < 8; ++i) v.push_back(uint8_t(ms >> (8 * i)));
    auto num = [&](const char* n, double d) {
        v.push_back(0); v.push_back(uint8_t(strlen(n))); put16(v, 8);
        v.insert(v.end(), n, n + strlen(n));
        uint8_t b[8]; memcpy(b, &d, 8); v.insert(v.end(), b, b + 8);
    };
    num("cpu.load", cpu); num("gpu.load", gpu); num("gpu.temp", temp);
    const char* s = "Test GPU";
    v.push_back(1); v.push_back(8); put16(v, uint16_t(strlen(s)));
    v.insert(v.end(), "gpu.name", "gpu.name" + 8); v.insert(v.end(), s, s + strlen(s));
    return v;
}

static void test_monitor() {
    // pure parts
    ql::MonitorSample s;
    auto snap = snapshot(3, 1000, 25, 80, NAN);
    CHECK(ql::parseSnapshot(snap.data(), snap.size(), s));
    CHECK(s.seq == 3 && s.unixMs == 1000 && s.cpuLoad == 25 && s.gpuLoad == 80 && s.gpuTemp == -1 && s.ramUsedPct == -1);
    CHECK(!ql::parseSnapshot(snap.data(), snap.size() - 1, s));       // truncated
    CHECK(ql::loadUnit(0, s) == 0.25 && ql::loadUnit(1, s) == 0.8 && ql::loadUnit(2, s) == -1 && ql::loadUnit(9, s) == -1);
    s.gpuTemp = 90; CHECK(ql::loadUnit(2, s) == 1.0);
    s.gpuTemp = 10; CHECK(ql::loadUnit(2, s) == 0.0);
    CHECK(ql::lerpColor(0, 0x2DD4BF, 0xFF0000) == 0x2DD4BF && ql::lerpColor(1, 0x2DD4BF, 0xFF0000) == 0xFF0000);
    CHECK(ql::lerpColor(0.5, 0x000000, 0xFF8040) == 0x804020 && ql::lerpColor(7, 0, 0x123456) == 0x123456);
    double in4[4] = {50, -1, 60, 10};
    CHECK(ql_load_unit(0, in4) == 0.5 && ql_load_unit(1, in4) == -1 && ql_load_unit(2, in4) == 0.5);
    CHECK(ql_lerp_color(1, 1, 2) == 2);

    // live: a writer (standing in for Euclase Monitor) and the link, through shared memory
    const char* ring = "/euclase-monitor-lighttest";
    CHECK(crd_create(ring, 64 * 1024, 0600, CRD_CREATE_REPLACE) == CRD_OK);
    crd_writer w;
    CHECK(crd_writer_open(&w, ring, 0) == CRD_OK);
    ql::MonitorLink link(ring);
    ql::MonitorSample got;
    CHECK(!link.poll(got, 100000));                                     // attached, but nothing published yet
    CHECK(link.attached());
    for (int i = 0; i < 3; ++i) {
        auto b = snapshot(uint64_t(i), 100000 + uint64_t(i), 10.0 * i, 5, 55);
        CHECK(crd_write(&w, EMS_RECORD_SNAPSHOT, b.data(), uint32_t(b.size())) == CRD_OK);
    }
    CHECK(link.poll(got, 100100) && got.seq == 2 && got.cpuLoad == 20 && got.gpuTemp == 55);   // newest wins
    CHECK(!link.poll(got, 100100 + 6000));                             // older than 5 s: stale
    crd_writer_close(&w);                                               // Monitor exits
    link.poll(got, 100200);
    CHECK(!link.attached());
    crd_unlink(ring);
    ql::MonitorLink none("/euclase-monitor-not-running");
    CHECK(!none.poll(got, 5000) && !none.attached());
    CHECK(ql_orgb_set_color(0, 0xFF0000) == 0);                          // not connected: refused, no crash
}

int main() {
    test_guard();
    test_effects();
    test_smbios();
    test_capi();
    test_openrgb();
    test_monitor();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
