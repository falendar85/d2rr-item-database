#pragma once

#include <memory>

namespace D2RL { struct PluginContext; }

namespace itemdb {
class PrototypeViewModel;

class OverlayHost {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    OverlayHost(PrototypeViewModel&, const D2RL::PluginContext*);
    ~OverlayHost();
    OverlayHost(const OverlayHost&) = delete;
    OverlayHost& operator=(const OverlayHost&) = delete;
    bool start();
    void toggle() noexcept;
    void stop() noexcept;
};
}
