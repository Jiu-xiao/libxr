#pragma once

#include <cstring>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "nvs.h"
#include "nvs_flash.h"

namespace LibXR
{

/**
 * @brief ESP32 NVS 数据库存储实现 / ESP32 NVS-backed database implementation
 */
class ESP32NvsFlashDatabase : public Database
{
 public:
  /**
   * @brief 构造并初始化 NVS / Construct and initialize NVS storage
   * @param namespace_name 命名空间名称 / Namespace name (default: "storage")
   */
  explicit ESP32NvsFlashDatabase(const char* namespace_name = "storage")
      : namespace_(namespace_name)
  {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    valid_ = (err == ESP_OK);
  }

  /**
   * @brief 查询初始化状态 / Check initialization state
   */
  bool IsValid() const { return valid_; }

 private:
  const char* namespace_;
  bool valid_{false};

  ErrorCode Add(KeyBase& key) override { return Set(key, key.raw_data_); }

  ErrorCode Get(KeyBase& key) override
  {
    if (!valid_) return ErrorCode::FAILED;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_, NVS_READONLY, &handle);
    if (err != ESP_OK) return ErrorCode::FAILED;

    size_t required_size = 0;
    err = nvs_get_blob(handle, key.name_, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
      nvs_close(handle);
      return ErrorCode::NOT_FOUND;
    }

    if (required_size != key.raw_data_.size_)
    {
      nvs_close(handle);
      return ErrorCode::FAILED;
    }

    err = nvs_get_blob(handle, key.name_, key.raw_data_.addr_, &required_size);
    nvs_close(handle);
    return (err == ESP_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  ErrorCode Set(KeyBase& key, RawData data) override
  {
    if (!valid_) return ErrorCode::FAILED;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_, NVS_READWRITE, &handle);
    if (err != ESP_OK) return ErrorCode::FAILED;

    err = nvs_set_blob(handle, key.name_, data.addr_, data.size_);
    if (err != ESP_OK)
    {
      nvs_close(handle);
      return ErrorCode::FAILED;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return (err == ESP_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }
};

}  // namespace LibXR
