// Reads live system load from Euclase Monitor through its Corundum shared-memory ring (no polling over HTTP,
// no system call per update). Used by the "react to system load" lighting mode.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ql {

struct MonitorSample {
    double cpuLoad = -1, gpuLoad = -1, gpuTemp = -1, ramUsedPct = -1;   // -1 = not reported
    uint64_t unixMs = 0, seq = 0;
};

// Pure: decodes one EMS1 snapshot (see metrics_wire.h). Returns false if it is malformed.
bool parseSnapshot(const void* data, size_t size, MonitorSample& out);

// 0..1 for a lighting source: 0 = CPU load, 1 = GPU load, 2 = GPU temperature (30..90 C), 3 = RAM use.
// Returns -1 when the value is not available.
double loadUnit(int source, const MonitorSample& s);

// Linear blend 0xRRGGBB a -> b, t clamped to 0..1.
uint32_t lerpColor(double t, uint32_t a, uint32_t b);

class MonitorLink {
public:
    explicit MonitorLink(const char* ring = nullptr) : ring_(ring) {}   // nullptr: Euclase Monitor's ring
    ~MonitorLink();
    MonitorLink(const MonitorLink&) = delete;
    MonitorLink& operator=(const MonitorLink&) = delete;

    // Attaches if needed (at most once per second while Monitor is not running), drains every pending
    // snapshot and keeps the newest. Returns true when `out` holds data younger than 5 seconds.
    bool poll(MonitorSample& out, uint64_t nowUnixMs);
    bool attached() const { return open_; }
    void close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    const char* ring_ = nullptr;
    bool open_ = false, have_ = false;
    uint64_t lastTry_ = 0, lastData_ = 0;
    MonitorSample last_;
};

}  // namespace ql
