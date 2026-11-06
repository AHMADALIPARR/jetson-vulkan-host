// ownership — the buffer ownership table (BUILD-PLAN pieces 3 and 8).
//
// Every buffer owned by the host has exactly one owning layer and one
// state. The GPU may touch a buffer only while the table records a
// transfer to the GPU layer, and every transfer is appended to a log
// the host prints and seals. Illegal transitions are refused, never
// silently applied.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class Owner { Host, Gpu, Display };
enum class BufState { Allocated, HostWriting, HostReady, GpuInFlight, GpuReady, Retired };

inline const char* toString(Owner o) {
    switch (o) {
        case Owner::Host: return "HOST";
        case Owner::Gpu: return "GPU";
        case Owner::Display: return "DISPLAY";
    }
    return "?";
}

inline const char* toString(BufState s) {
    switch (s) {
        case BufState::Allocated: return "ALLOCATED";
        case BufState::HostWriting: return "HOST_WRITING";
        case BufState::HostReady: return "HOST_READY";
        case BufState::GpuInFlight: return "GPU_IN_FLIGHT";
        case BufState::GpuReady: return "GPU_READY";
        case BufState::Retired: return "RETIRED";
    }
    return "?";
}

struct BufferRecord {
    uint32_t id = 0;
    std::string name;
    size_t bytes = 0;
    Owner owner = Owner::Host;
    BufState state = BufState::Allocated;
};

struct Transfer {
    uint32_t bufferId;
    Owner from;
    Owner to;
    BufState resultingState;
    std::string note;
};

class OwnershipTable {
public:
    uint32_t addBuffer(const std::string& name, size_t bytes) {
        BufferRecord r;
        r.id = nextId_++;
        r.name = name;
        r.bytes = bytes;
        buffers_.push_back(r);
        return r.id;
    }

    const BufferRecord* find(uint32_t id) const {
        for (const auto& b : buffers_) if (b.id == id) return &b;
        return nullptr;
    }

    // Legal transition graph — the only transfers the table accepts.
    static bool legal(BufState from, BufState to) {
        switch (from) {
            case BufState::Allocated:   return to == BufState::HostWriting || to == BufState::Retired;
            case BufState::HostWriting: return to == BufState::HostReady;
            case BufState::HostReady:   return to == BufState::GpuInFlight || to == BufState::HostWriting;
            case BufState::GpuInFlight: return to == BufState::GpuReady;
            case BufState::GpuReady:    return to == BufState::HostReady || to == BufState::Retired;
            case BufState::Retired:     return false;
        }
        return false;
    }

    bool transition(uint32_t id, Owner to, BufState newState, const std::string& note) {
        for (auto& b : buffers_) {
            if (b.id != id) continue;
            if (!legal(b.state, newState)) return false;
            Transfer t{b.id, b.owner, to, newState, note};
            b.owner = to;
            b.state = newState;
            log_.push_back(t);
            return true;
        }
        return false;
    }

    const std::vector<BufferRecord>& buffers() const { return buffers_; }
    const std::vector<Transfer>& log() const { return log_; }

private:
    std::vector<BufferRecord> buffers_;
    std::vector<Transfer> log_;
    uint32_t nextId_ = 1;
};
