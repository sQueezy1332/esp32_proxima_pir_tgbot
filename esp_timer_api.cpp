#include "esp_timer_api.h"
#include "esp_err.h"

esp_timer_handle_t esp_timer_init(esp_timer_cb_t cb, esp_timer_dispatch_t type, bool skip, void* arg, const char* name) {
    esp_timer_handle_t handle; esp_err_t ret;
    esp_timer_create_args_t config = {
      .callback = cb,
      .arg = arg,
      .dispatch_method = type,
      .name = name,
      .skip_unhandled_events = skip,
    };
    ret = esp_timer_create(&config, &handle);
    if (ret != ESP_OK) { ESP_ERROR_CHECK_WITHOUT_ABORT(ret); return NULL; };
    return handle;
}
  