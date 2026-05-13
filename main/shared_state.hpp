#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "avatar/expression.hpp"

namespace stackchan::app {

// State shared between the demo / servo / render tasks. Most fields are
// lock-free atomics; the balloon text needs a mutex because std::string
// isn't trivially copyable.
class SharedState {
public:
    std::atomic<float> target_yaw_deg{0.0f};
    std::atomic<float> target_pitch_deg{0.0f};
    std::atomic<float> mouth_open{0.0f};
    std::atomic<int> expression{static_cast<int>(avatar::Expression::Neutral)};

    void set_balloon_text(std::string_view text)
    {
        std::lock_guard lock{balloon_mutex_};
        balloon_text_.assign(text);
        balloon_version_.fetch_add(1, std::memory_order_release);
        balloon_visible_.store(true, std::memory_order_release);
    }

    void clear_balloon()
    {
        std::lock_guard lock{balloon_mutex_};
        balloon_text_.clear();
        balloon_version_.fetch_add(1, std::memory_order_release);
        balloon_visible_.store(false, std::memory_order_release);
    }

    // Returns the current balloon version (incremented on every change).
    std::uint32_t balloon_version() const noexcept
    {
        return balloon_version_.load(std::memory_order_acquire);
    }

    bool balloon_visible() const noexcept
    {
        return balloon_visible_.load(std::memory_order_acquire);
    }

    // Copies the current text into `out` (`out` is replaced).
    void copy_balloon_text(std::string& out) const
    {
        std::lock_guard lock{balloon_mutex_};
        out = balloon_text_;
    }

private:
    mutable std::mutex balloon_mutex_;
    std::string balloon_text_;
    std::atomic<std::uint32_t> balloon_version_{0};
    std::atomic<bool> balloon_visible_{false};
};

} // namespace stackchan::app
