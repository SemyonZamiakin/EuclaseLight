#include "ql/monitor_link.h"
#include "ql/metrics_wire.h"
#include "../third_party/corundum/corundum.h"
#include <cmath>

namespace ql {

struct MonitorLink::Impl { crd_reader r; };

bool parseSnapshot(const void* data, size_t size, MonitorSample& out) {
    ems_header h;
    ems_iter it;
    if (!ems_begin(data, size, &h, &it)) return false;
    MonitorSample s;
    s.seq = h.seq;
    s.unixMs = h.unix_ms;
    ems_entry e;
    uint32_t n = 0;
    while (ems_next(&it, &e)) {
        ++n;
        if (e.kind != EMS_KIND_NUMBER || std::isnan(e.number)) continue;
        if (ems_is(&e, "cpu.load")) s.cpuLoad = e.number;
        else if (ems_is(&e, "gpu.load")) s.gpuLoad = e.number;
        else if (ems_is(&e, "gpu.temp")) s.gpuTemp = e.number;
        else if (ems_is(&e, "ram.used_pct")) s.ramUsedPct = e.number;
    }
    if (n != h.count) return false;
    out = s;
    return true;
}

static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

double loadUnit(int source, const MonitorSample& s) {
    switch (source) {
        case 0: return s.cpuLoad < 0 ? -1 : clamp01(s.cpuLoad / 100.0);
        case 1: return s.gpuLoad < 0 ? -1 : clamp01(s.gpuLoad / 100.0);
        case 2: return s.gpuTemp < 0 ? -1 : clamp01((s.gpuTemp - 30.0) / 60.0);
        case 3: return s.ramUsedPct < 0 ? -1 : clamp01(s.ramUsedPct / 100.0);
        default: return -1;
    }
}

uint32_t lerpColor(double t, uint32_t a, uint32_t b) {
    t = clamp01(t);
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const double ca = double((a >> sh) & 0xFF), cb = double((b >> sh) & 0xFF);
        out |= uint32_t(std::lround(ca + (cb - ca) * t)) << sh;
    }
    return out;
}

MonitorLink::~MonitorLink() { close(); }

void MonitorLink::close() {
    if (impl_) {
        if (open_) crd_reader_close(&impl_->r);
        delete impl_;
        impl_ = nullptr;
    }
    open_ = false;
}

bool MonitorLink::poll(MonitorSample& out, uint64_t now) {
    if (!open_ && now - lastTry_ >= 1000) {
        lastTry_ = now;
        if (!impl_) impl_ = new Impl();
        open_ = crd_reader_open(&impl_->r, ring_ ? ring_ : EMS_RING_NAME, 0) == CRD_OK;
        if (open_) lastData_ = now;
    }
    if (open_) {
        crd_msg m;
        crd_status st;
        while ((st = crd_read(&impl_->r, &m)) == CRD_OK) {
            MonitorSample s;
            if (m.type == EMS_RECORD_SNAPSHOT && parseSnapshot(m.data, m.size, s)) {   // keep the newest
                last_ = s;
                have_ = true;
                lastData_ = now;
            }
        }
        crd_release(&impl_->r);
        // Monitor exited, the ring is corrupt, or nothing arrived for 5 s (Monitor restarted elsewhere): re-attach
        const bool closed = crd_writer_closed(&impl_->r) && crd_reader_pending(&impl_->r) == 0;
        if (st == CRD_ECORRUPT || closed || now - lastData_ > 5000) close();
    }
    if (have_ && now >= last_.unixMs && now - last_.unixMs <= 5000) {
        out = last_;
        return true;
    }
    return false;
}

}  // namespace ql
