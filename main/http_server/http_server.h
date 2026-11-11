// Management HTTP API.
//
// The server is started only while the management AP is active.  All mutations
// require the single editor token from the lease state machine; uploads stream
// to a temporary file and replace the previous asset only after header, size,
// and CRC validation.  Data pointers belong to the application core and are not
// read by the UI while management mode is active.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "data/games.h"
#include "data/nfc.h"
#include "data/profile.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "session/mgmt_session.h"
#include "storage/storage.h"

typedef struct {
    const pp_storage_t *storage;
    pp_profile_t *profile;
    pp_profile_images_t *images;
    pp_games_t *games;
    pp_nfc_t *nfc;
    pp_mgmt_session_t *session;
    SemaphoreHandle_t session_lock;
    const char *ap_ssid;
    const char *ap_ip;
    int (*client_count)(void);
} pp_http_context_t;

esp_err_t pp_http_server_start(const pp_http_context_t *context);
void pp_http_server_stop(void);
bool pp_http_server_running(void);
