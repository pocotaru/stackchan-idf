#include <cmath>
#include <cstdint>

#include <M5Unified.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "avatar/expression.hpp"
#include "board/board.hpp"
#include "render_task.hpp"
#include "servo_task.hpp"
#include "shared_state.hpp"

namespace {

constexpr const char* kTag = "stackchan";

// Heap-allocate so the task argument outlives app_main's scope (the tasks run forever).
stackchan::app::SharedState* g_state = nullptr;
stackchan::app::RenderTaskArgs* g_render_args = nullptr;
stackchan::app::ServoTaskArgs* g_servo_args = nullptr;

void demo_loop()
{
    using namespace stackchan;

    constexpr avatar::Expression kCycle[] = {
        avatar::Expression::Neutral, avatar::Expression::Happy, avatar::Expression::Doubt,
        avatar::Expression::Sad,     avatar::Expression::Angry, avatar::Expression::Sleepy,
    };
    // Discrete head targets — PathGenerator interpolates smoothly between them.
    // (Continuously changing yaw_deg would restart begin_move_to every tick and
    //  the servo would never make visible progress.)
    constexpr float kYawTargets[] = {0.0f, 20.0f, 0.0f, -20.0f};
    constexpr std::uint32_t kPosePeriodMs = 2000;
    constexpr std::uint32_t kExpressionPeriodMs = 3000;

    std::size_t expression_index = 0;
    std::size_t pose_index = 0;
    std::uint32_t next_expression_ms = 0;
    std::uint32_t next_pose_ms = 0;

    for (;;) {
        const std::uint32_t now_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);

        // Mouth: smooth bobble at ~0.5 Hz drives only the avatar, not a servo,
        // so it's safe to update continuously.
        const float t = static_cast<float>(now_ms) / 1000.0f;
        const float mouth = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14159265f / 2.0f);
        g_state->mouth_open.store(mouth, std::memory_order_relaxed);

        // Step-wise yaw target every 2 s so the path generator can run.
        if (now_ms >= next_pose_ms) {
            g_state->target_yaw_deg.store(kYawTargets[pose_index], std::memory_order_relaxed);
            pose_index = (pose_index + 1) % (sizeof(kYawTargets) / sizeof(kYawTargets[0]));
            next_pose_ms = now_ms + kPosePeriodMs;
        }

        // Cycle expression every 3 s.
        if (now_ms >= next_expression_ms) {
            g_state->expression.store(static_cast<int>(kCycle[expression_index]), std::memory_order_relaxed);
            expression_index = (expression_index + 1) % (sizeof(kCycle) / sizeof(kCycle[0]));
            next_expression_ms = now_ms + kExpressionPeriodMs;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

} // namespace

extern "C" void app_main()
{
    auto board_result = stackchan::board::Board::begin();
    if (!board_result) {
        ESP_LOGE(kTag, "Board::begin() failed: %d", static_cast<int>(board_result.error()));
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    auto& board = *board_result;

    if (auto r = board.set_servo_power(true); !r) {
        ESP_LOGE(kTag, "set_servo_power(true) failed: %d", static_cast<int>(r.error()));
    }
    // Allow the servo bus rail to settle before the servo task starts driving UART.
    vTaskDelay(pdMS_TO_TICKS(500));

    g_state = new stackchan::app::SharedState{};
    g_render_args = new stackchan::app::RenderTaskArgs{.display = &board.display(), .state = g_state};
    g_servo_args = new stackchan::app::ServoTaskArgs{.state = g_state};

    stackchan::app::start_render_task(*g_render_args);
    stackchan::app::start_servo_task(*g_servo_args);

    ESP_LOGI(kTag, "ready");
    demo_loop();
}
