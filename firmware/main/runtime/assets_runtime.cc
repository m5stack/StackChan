#include "assets.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "lvgl_theme.h"

#if HAVE_LVGL
#include "display/lcd_display.h"
#include <spi_flash_mmap.h>
#endif

#include <cbin_font.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace {
constexpr const char* kTag = "Assets";
constexpr const char* kPartitionLabel = "assets";

struct mmap_assets_table {
    char asset_name[32];
    uint32_t asset_size;
    uint32_t asset_offset;
    uint16_t asset_width;
    uint16_t asset_height;
};
}

Assets::Assets()
{
#if HAVE_LVGL
    strategy_ = std::make_unique<Assets::LvglStrategy>();
#else
    strategy_ = std::make_unique<Assets::EmoteStrategy>();
#endif
    InitializePartition();
}

Assets::~Assets()
{
    UnApplyPartition();
}

bool Assets::FindPartition(Assets* assets)
{
    assets->partition_ =
        esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, kPartitionLabel);
    if (assets->partition_ == nullptr) {
        ESP_LOGI(kTag, "No assets partition found");
        return false;
    }
    return true;
}

bool Assets::Apply(bool refresh_display_theme)
{
    return strategy_ ? strategy_->Apply(this, refresh_display_theme) : false;
}

bool Assets::InitializePartition()
{
    return strategy_ ? strategy_->InitializePartition(this) : false;
}

void Assets::UnApplyPartition()
{
    if (strategy_) {
        strategy_->UnApplyPartition(this);
    }
}

bool Assets::GetAssetData(const std::string& name, void*& ptr, size_t& size)
{
    return strategy_ ? strategy_->GetAssetData(this, name, ptr, size) : false;
}

bool Assets::LoadSrmodelsFromIndex(Assets* assets, cJSON* root)
{
    void* ptr = nullptr;
    size_t size = 0;
    bool need_delete_root = false;

    if (root == nullptr) {
        if (!assets->GetAssetData("index.json", ptr, size)) {
            ESP_LOGE(kTag, "The index.json file is not found");
            return false;
        }

        root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
        if (root == nullptr) {
            ESP_LOGE(kTag, "The index.json file is not valid");
            return false;
        }
        need_delete_root = true;
    }

    cJSON* srmodels = cJSON_GetObjectItem(root, "srmodels");
    if (cJSON_IsString(srmodels)) {
        std::string srmodels_file = srmodels->valuestring;
        if (assets->GetAssetData(srmodels_file, ptr, size)) {
            if (assets->models_list_ != nullptr) {
                esp_srmodel_deinit(assets->models_list_);
                assets->models_list_ = nullptr;
            }
            assets->models_list_ = srmodel_load(static_cast<uint8_t*>(ptr));
            if (assets->models_list_ != nullptr) {
                auto& app = Application::GetInstance();
                app.GetAudioService().SetModelsList(assets->models_list_);
                if (need_delete_root) {
                    cJSON_Delete(root);
                }
                return true;
            }
            ESP_LOGE(kTag, "Failed to load srmodels.bin");
        } else {
            ESP_LOGE(kTag, "The srmodels file %s is not found", srmodels_file.c_str());
        }
    }

    if (need_delete_root) {
        cJSON_Delete(root);
    }
    return false;
}

#if HAVE_LVGL
uint32_t Assets::LvglStrategy::CalculateChecksum(const char* data, uint32_t length)
{
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < length; ++i) {
        checksum += data[i];
    }
    return checksum & 0xFFFF;
}

bool Assets::LvglStrategy::InitializePartition(Assets* assets)
{
    assets->partition_valid_ = false;
    assets_.clear();

    if (!Assets::FindPartition(assets)) {
        return false;
    }

    const int free_pages = spi_flash_mmap_get_free_pages(SPI_FLASH_MMAP_DATA);
    const uint32_t storage_size = free_pages * 64 * 1024;
    ESP_LOGI(kTag, "The storage free size is %ld KB", storage_size / 1024);
    ESP_LOGI(kTag, "The partition size is %ld KB", assets->partition_->size / 1024);
    if (storage_size < assets->partition_->size) {
        ESP_LOGE(kTag, "The free size %ld KB is less than assets partition required %ld KB", storage_size / 1024,
                 assets->partition_->size / 1024);
        return false;
    }

    const esp_err_t err =
        esp_partition_mmap(assets->partition_, 0, assets->partition_->size, ESP_PARTITION_MMAP_DATA,
                           reinterpret_cast<const void**>(&mmap_root_), &mmap_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to mmap assets partition: %s", esp_err_to_name(err));
        return false;
    }

    assets->partition_valid_ = true;

    const uint32_t stored_files = *reinterpret_cast<const uint32_t*>(mmap_root_ + 0);
    const uint32_t stored_chksum = *reinterpret_cast<const uint32_t*>(mmap_root_ + 4);
    const uint32_t stored_len = *reinterpret_cast<const uint32_t*>(mmap_root_ + 8);

    if (stored_len > assets->partition_->size - 12) {
        ESP_LOGD(kTag, "The stored_len (0x%lx) is greater than the partition size (0x%lx) - 12", stored_len,
                 assets->partition_->size);
        return false;
    }

    const int64_t start_time = esp_timer_get_time();
    const uint32_t calculated_checksum = CalculateChecksum(mmap_root_ + 12, stored_len);
    const int64_t end_time = esp_timer_get_time();
    ESP_LOGI(kTag, "The checksum calculation time is %d ms", int((end_time - start_time) / 1000));

    if (calculated_checksum != stored_chksum) {
        ESP_LOGE(kTag, "The calculated checksum (0x%lx) does not match the stored checksum (0x%lx)",
                 calculated_checksum, stored_chksum);
        return false;
    }

    checksum_valid_ = true;

    for (uint32_t i = 0; i < stored_files; ++i) {
        const auto* item = reinterpret_cast<const mmap_assets_table*>(mmap_root_ + 12 + i * sizeof(mmap_assets_table));
        assets_[item->asset_name] = Asset{
            .size = static_cast<size_t>(item->asset_size),
            .offset = static_cast<size_t>(12 + sizeof(mmap_assets_table) * stored_files + item->asset_offset),
        };
    }
    return checksum_valid_;
}

void Assets::LvglStrategy::UnApplyPartition(Assets* assets)
{
    if (mmap_handle_ != 0) {
        esp_partition_munmap(mmap_handle_);
        mmap_handle_ = 0;
        mmap_root_ = nullptr;
    }
    checksum_valid_ = false;
    assets_.clear();
    (void)assets;
}

bool Assets::LvglStrategy::GetAssetData(Assets* assets, const std::string& name, void*& ptr, size_t& size)
{
    auto asset = assets_.find(name);
    if (asset == assets_.end()) {
        return false;
    }
    const char* data = mmap_root_ + asset->second.offset;
    if (data[0] != 'Z' || data[1] != 'Z') {
        ESP_LOGE(kTag, "The asset %s is not valid with magic %02x%02x", name.c_str(), data[0], data[1]);
        return false;
    }

    ptr = const_cast<char*>(data + 2);
    size = asset->second.size;
    return true;
}

bool Assets::LvglStrategy::Apply(Assets* assets, bool refresh_display_theme)
{
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets->GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(kTag, "The index.json file is not found");
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(kTag, "The index.json file is not valid");
        return false;
    }

    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsNumber(version) && version->valuedouble > 1) {
        ESP_LOGE(kTag, "The assets version %d is not supported, please upgrade the firmware", version->valueint);
        cJSON_Delete(root);
        return false;
    }

    Assets::LoadSrmodelsFromIndex(assets, root);

    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");

    cJSON* font = cJSON_GetObjectItem(root, "text_font");
    if (cJSON_IsString(font)) {
        std::string fonts_text_file = font->valuestring;
        if (assets->GetAssetData(fonts_text_file, ptr, size)) {
            auto text_font = std::make_shared<LvglCBinFont>(ptr);
            if (text_font->font() == nullptr) {
                ESP_LOGE(kTag, "Failed to load fonts.bin");
                cJSON_Delete(root);
                return false;
            }
            if (light_theme != nullptr) {
                light_theme->set_text_font(text_font);
            }
            if (dark_theme != nullptr) {
                dark_theme->set_text_font(text_font);
            }
        } else {
            ESP_LOGE(kTag, "The font file %s is not found", fonts_text_file.c_str());
        }
    }

    cJSON* emoji_collection = cJSON_GetObjectItem(root, "emoji_collection");
    if (cJSON_IsArray(emoji_collection)) {
        auto custom_emoji_collection = std::make_shared<EmojiCollection>();
        const int emoji_count = cJSON_GetArraySize(emoji_collection);
        for (int i = 0; i < emoji_count; ++i) {
            cJSON* emoji = cJSON_GetArrayItem(emoji_collection, i);
            if (!cJSON_IsObject(emoji)) {
                continue;
            }
            cJSON* name = cJSON_GetObjectItem(emoji, "name");
            cJSON* file = cJSON_GetObjectItem(emoji, "file");
            cJSON* eaf = cJSON_GetObjectItem(emoji, "eaf");
            if (cJSON_IsString(name) && cJSON_IsString(file) && eaf == nullptr) {
                if (!assets->GetAssetData(file->valuestring, ptr, size)) {
                    ESP_LOGE(kTag, "Emoji %s image file %s is not found", name->valuestring, file->valuestring);
                    continue;
                }
                custom_emoji_collection->AddEmoji(name->valuestring, new LvglRawImage(ptr, size));
            }
        }
        if (light_theme != nullptr) {
            light_theme->set_emoji_collection(custom_emoji_collection);
        }
        if (dark_theme != nullptr) {
            dark_theme->set_emoji_collection(custom_emoji_collection);
        }
    }

    cJSON* skin = cJSON_GetObjectItem(root, "skin");
    if (cJSON_IsObject(skin)) {
        cJSON* light_skin = cJSON_GetObjectItem(skin, "light");
        if (cJSON_IsObject(light_skin) && light_theme != nullptr) {
            cJSON* text_color = cJSON_GetObjectItem(light_skin, "text_color");
            cJSON* background_color = cJSON_GetObjectItem(light_skin, "background_color");
            cJSON* background_image = cJSON_GetObjectItem(light_skin, "background_image");
            if (cJSON_IsString(text_color)) {
                light_theme->set_text_color(LvglTheme::ParseColor(text_color->valuestring));
            }
            if (cJSON_IsString(background_color)) {
                light_theme->set_background_color(LvglTheme::ParseColor(background_color->valuestring));
                light_theme->set_chat_background_color(LvglTheme::ParseColor(background_color->valuestring));
            }
            if (cJSON_IsString(background_image)) {
                if (!assets->GetAssetData(background_image->valuestring, ptr, size)) {
                    ESP_LOGE(kTag, "The background image file %s is not found", background_image->valuestring);
                    cJSON_Delete(root);
                    return false;
                }
                auto background = std::make_shared<LvglCBinImage>(ptr);
                light_theme->set_background_image(background);
            }
        }

        cJSON* dark_skin = cJSON_GetObjectItem(skin, "dark");
        if (cJSON_IsObject(dark_skin) && dark_theme != nullptr) {
            cJSON* text_color = cJSON_GetObjectItem(dark_skin, "text_color");
            cJSON* background_color = cJSON_GetObjectItem(dark_skin, "background_color");
            cJSON* background_image = cJSON_GetObjectItem(dark_skin, "background_image");
            if (cJSON_IsString(text_color)) {
                dark_theme->set_text_color(LvglTheme::ParseColor(text_color->valuestring));
            }
            if (cJSON_IsString(background_color)) {
                dark_theme->set_background_color(LvglTheme::ParseColor(background_color->valuestring));
                dark_theme->set_chat_background_color(LvglTheme::ParseColor(background_color->valuestring));
            }
            if (cJSON_IsString(background_image)) {
                if (!assets->GetAssetData(background_image->valuestring, ptr, size)) {
                    ESP_LOGE(kTag, "The background image file %s is not found", background_image->valuestring);
                    cJSON_Delete(root);
                    return false;
                }
                auto background = std::make_shared<LvglCBinImage>(ptr);
                dark_theme->set_background_image(background);
            }
        }
    }

    if (refresh_display_theme) {
        auto* display = Board::GetInstance().GetDisplay();
        ESP_LOGI(kTag, "Refreshing display theme...");

        auto current_theme = display->GetTheme();
        if (current_theme != nullptr) {
            display->SetTheme(current_theme);
        }

        cJSON* hide_subtitle = cJSON_GetObjectItem(root, "hide_subtitle");
        if (cJSON_IsBool(hide_subtitle)) {
            const bool hide = cJSON_IsTrue(hide_subtitle);
            auto* lcd_display = dynamic_cast<LcdDisplay*>(display);
            if (lcd_display != nullptr) {
                lcd_display->SetHideSubtitle(hide);
                ESP_LOGI(kTag, "Set hide_subtitle to %s", hide ? "true" : "false");
            }
        }
    }

    cJSON_Delete(root);
    return true;
}
#endif

bool Assets::Download(std::string url, std::function<void(int progress, size_t speed)> progress_callback)
{
    ESP_LOGI(kTag, "Downloading new version of assets from %s", url.c_str());

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    if (!http->Open("GET", url)) {
        ESP_LOGE(kTag, "Failed to open HTTP connection");
        return false;
    }
    if (http->GetStatusCode() != 200) {
        ESP_LOGE(kTag, "Failed to get assets, status code: %d", http->GetStatusCode());
        return false;
    }

    const size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(kTag, "Failed to get content length");
        return false;
    }
    if (content_length > partition_->size) {
        ESP_LOGE(kTag, "Assets file size (%u) is larger than partition size (%lu)", content_length, partition_->size);
        return false;
    }

    constexpr size_t kHeaderSize = 12;
    if (content_length < kHeaderSize) {
        ESP_LOGE(kTag, "Content length (%u) is smaller than header size (%u)", content_length, kHeaderSize);
        return false;
    }

    const size_t sector_size = esp_partition_get_main_flash_sector_size();
    using BufferPtr = std::unique_ptr<char, decltype(&heap_caps_free)>;
    BufferPtr buffer(static_cast<char*>(heap_caps_malloc(sector_size, MALLOC_CAP_INTERNAL)), &heap_caps_free);
    if (!buffer) {
        ESP_LOGE(kTag, "Failed to allocate buffer");
        return false;
    }

    UnApplyPartition();

    size_t total_written = 0;
    size_t recent_written = 0;
    size_t current_sector = 0;
    int64_t last_calc_time = esp_timer_get_time();
    uint8_t header_buf[kHeaderSize] = {};
    size_t header_collected = 0;
    bool success = false;

    while (true) {
        const int ret = http->Read(buffer.get(), sector_size);
        if (ret < 0) {
            ESP_LOGE(kTag, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            break;
        }
        if (ret == 0) {
            success = true;
            break;
        }

        size_t buf_pos = 0;
        if (header_collected < kHeaderSize) {
            const size_t need = kHeaderSize - header_collected;
            const size_t take = std::min(static_cast<size_t>(ret), need);
            memcpy(header_buf + header_collected, buffer.get(), take);
            header_collected += take;
            buf_pos += take;
        }

        if (static_cast<size_t>(ret) > buf_pos) {
            const size_t write_len = static_cast<size_t>(ret) - buf_pos;
            const size_t write_end_offset = kHeaderSize + total_written + write_len;
            const size_t needed_sectors = (write_end_offset + sector_size - 1) / sector_size;
            bool erase_failed = false;

            while (current_sector < needed_sectors) {
                const size_t sector_start = current_sector * sector_size;
                const size_t sector_end = sector_start + sector_size;
                if (sector_end > partition_->size) {
                    ESP_LOGE(kTag, "Sector end (%u) exceeds partition size (%lu)", sector_end, partition_->size);
                    erase_failed = true;
                    break;
                }

                const esp_err_t err = esp_partition_erase_range(partition_, sector_start, sector_size);
                if (err != ESP_OK) {
                    ESP_LOGE(kTag, "Failed to erase sector %u at offset %u: %s", current_sector, sector_start,
                             esp_err_to_name(err));
                    erase_failed = true;
                    break;
                }
                ++current_sector;
            }

            if (erase_failed) {
                break;
            }

            const esp_err_t err =
                esp_partition_write(partition_, kHeaderSize + total_written, buffer.get() + buf_pos, write_len);
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "Failed to write to assets partition at offset %u: %s",
                         static_cast<unsigned int>(kHeaderSize + total_written), esp_err_to_name(err));
                break;
            }

            total_written += write_len;
            recent_written += write_len;
        }

        if (esp_timer_get_time() - last_calc_time >= 1000000 ||
            (header_collected + total_written) == content_length) {
            const size_t progress = (header_collected + total_written) * 100 / content_length;
            if (progress_callback) {
                progress_callback(progress, recent_written);
            }
            last_calc_time = esp_timer_get_time();
            recent_written = 0;
        }
    }

    if (success && (header_collected + total_written != content_length)) {
        ESP_LOGE(kTag, "Downloaded size (%u) does not match expected size (%u)",
                 static_cast<unsigned int>(header_collected + total_written), static_cast<unsigned int>(content_length));
        success = false;
    }

    if (success) {
        const esp_err_t err = esp_partition_write(partition_, 0, header_buf, kHeaderSize);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to write assets header to partition: %s", esp_err_to_name(err));
            success = false;
        }
    }

    if (!success) {
        ESP_LOGE(kTag, "Assets download failed");
        return false;
    }

    if (!InitializePartition()) {
        ESP_LOGE(kTag, "Failed to re-initialize assets partition");
        return false;
    }

    return true;
}
