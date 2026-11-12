#include "agent_tasks_setup_worker.h"
#include "agent_tasks_poller.h"
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace smooth_ui_toolkit::lvgl_cpp;
LV_FONT_DECLARE(agent_tasks_cjk_14);

namespace agent_tasks {
void SetupWorker::edit(bool token)
{
    if (editor_) return;
    editing_token_ = token;
    editor_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(editor_, 320, 240);
    lv_obj_set_style_pad_all(editor_, 0, 0);
    lv_obj_set_style_border_width(editor_, 0, 0);
    lv_obj_set_style_radius(editor_, 0, 0);
    lv_obj_remove_flag(editor_, LV_OBJ_FLAG_SCROLLABLE);
    auto* heading = lv_label_create(editor_);
    lv_obj_set_style_text_font(heading, &agent_tasks_cjk_14, 0);
    lv_label_set_text(heading, token ? "访问令牌" : "网关地址");
    lv_obj_set_pos(heading, 10, 4);
    input_ = lv_textarea_create(editor_);
    lv_obj_set_size(input_, 300, 40);
    lv_obj_set_pos(input_, 10, 29);
    lv_textarea_set_one_line(input_, true);
    lv_textarea_set_max_length(input_, token ? 256 : 255);
    lv_textarea_set_password_mode(input_, token);
    lv_textarea_set_text(input_, (token ? config_.token : config_.url).c_str());
    auto* keyboard = lv_keyboard_create(editor_);
    lv_obj_set_size(keyboard, 320, 165);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, input_);
    lv_obj_add_event_cb(keyboard, [](lv_event_t* event) {
        auto* self = static_cast<SetupWorker*>(lv_event_get_user_data(event));
        const auto code = lv_event_get_code(event);
        if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
        if (code == LV_EVENT_READY) {
            (self->editing_token_ ? self->config_.token : self->config_.url) =
                lv_textarea_get_text(self->input_);
            self->test_result_->result = 0;
            self->save_failed_ = false;
        }
        lv_obj_delete_async(self->editor_);
        self->editor_ = nullptr;
        self->input_ = nullptr;
    }, LV_EVENT_ALL, this);
}

SetupWorker::SetupWorker()
{
    save_failed_ = !load_config(config_);
    title_ = std::make_unique<Label>(lv_screen_active()); title_->setText("Agent 看板设置");
    title_->setTextFont(&agent_tasks_cjk_14); title_->setPos(16, 10);
    url_label_ = std::make_unique<Label>(lv_screen_active()); url_label_->setWidth(290); url_label_->setPos(16, 48);
    url_label_->setTextFont(&agent_tasks_cjk_14); url_label_->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    token_label_ = std::make_unique<Label>(lv_screen_active()); token_label_->setWidth(290); token_label_->setPos(16, 78);
    token_label_->setTextFont(&agent_tasks_cjk_14);
    result_ = std::make_unique<Label>(lv_screen_active()); result_->setPos(16, 110);
    result_->setTextFont(&agent_tasks_cjk_14);
    auto button = [](int x, int y, int width, const char* text) {
        auto value = std::make_unique<Button>(lv_screen_active());
        value->setSize(width, 38); value->setPos(x, y);
        value->label().setTextFont(&agent_tasks_cjk_14); value->label().setText(text);
        return value;
    };
    url_button_ = button(12, 145, 140, "设置地址");
    token_button_ = button(166, 145, 140, "设置令牌");
    test_button_ = button(10, 192, 90, "测试");
    save_button_ = button(112, 192, 90, "保存");
    cancel_button_ = button(214, 192, 90, "取消");
    url_button_->onClick().connect([this]() { edit(false); });
    token_button_->onClick().connect([this]() { edit(true); });
    test_button_->onClick().connect([this]() {
        if (test_result_->running) return;
        using Args = std::pair<std::shared_ptr<TestResult>, Config>;
        auto* args = new Args(test_result_, config_);
        test_result_->running = true;
        test_result_->result = 0;
        if (xTaskCreate([](void* p) {
            std::unique_ptr<Args> args(static_cast<Args*>(p));
            {
                Poller poller;
                poller.test_once(args->second);
                args->first->result = poller.connected() ? 1 : -1;
            }
            args->first->running = false;
            args.reset();
            vTaskDelete(nullptr);
        }, "agent_test", 6144, args, 2, nullptr) != pdPASS) {
            delete args;
            test_result_->running = false;
            test_result_->result = -1;
        }
    });
    save_button_->onClick().connect([this]() {
        _is_done = save_config(config_);
        save_failed_ = !_is_done;
    });
    cancel_button_->onClick().connect([this]() { _is_done = true; });
    update();
}

SetupWorker::~SetupWorker()
{
    if (editor_) lv_obj_delete(editor_);
}

void SetupWorker::update()
{
    url_label_->setText(("地址: " + (config_.url.empty() ? "未配置" : config_.url)).c_str());
    token_label_->setText(config_.token.empty() ? "令牌: 未配置" : "令牌: ********");
    if (save_failed_) result_->setText("配置读写失败，请重试");
    else if (test_result_->running) result_->setText("正在测试连接");
    else if (test_result_->result == 1) result_->setText("连接成功");
    else if (test_result_->result == -1) result_->setText("连接失败");
    else result_->setText("");
}
}
