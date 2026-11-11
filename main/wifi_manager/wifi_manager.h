// WPA2 management access point.
//
// Normal card mode keeps the radio off.  Entering management mode creates the
// AP, DHCP server, and event handlers; leaving tears them down so repeat cycles
// do not leak sockets, netifs, or Wi-Fi state.  The password is generated once
// and stored in NVS, then displayed only on the device's management screen.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    bool started;
    char ssid[33];
    char password[64];
    char ip[16];
    int client_count;
} pp_wifi_status_t;

// Initializes NVS/netif/event loop as needed, starts AP mode, and waits for
// WIFI_EVENT_AP_START (5 s).  On any failure the partially started stack is
// rolled back and the error is returned.
esp_err_t pp_wifi_manager_start(pp_wifi_status_t *status);

// Stops HTTP-independent Wi-Fi resources.  The caller must stop the HTTP server
// first so no request is in flight.  Safe to call when not started.
void pp_wifi_manager_stop(void);

bool pp_wifi_manager_running(void);
int pp_wifi_manager_client_count(void);

// Returns the fixed product AP password.  It is displayed on the device's
// management screen and is not stored in NVS.
esp_err_t pp_wifi_manager_password(char *out, size_t out_size);
