#pragma once
#include <apps/app_setup/workers/workers.h>
#include "agent_tasks_config.h"
#include <memory>
#include <atomic>
#include <uitk/short_namespace.hpp>

namespace agent_tasks {
class SetupWorker : public setup_workers::WorkerBase {
public:
    SetupWorker();
    ~SetupWorker() override;
    void update() override;
private:
    Config config_;
    std::unique_ptr<uitk::lvgl_cpp::Label> title_, url_label_, token_label_, result_;
    std::unique_ptr<uitk::lvgl_cpp::Button> url_button_, token_button_, test_button_, save_button_, cancel_button_;
    struct TestResult { std::atomic_bool running{false}; std::atomic_int result{0}; };
    std::shared_ptr<TestResult> test_result_ = std::make_shared<TestResult>();
    lv_obj_t* editor_ = nullptr;
    lv_obj_t* input_ = nullptr;
    bool editing_token_ = false;
    bool save_failed_ = false;
    void edit(bool token);
};
}
