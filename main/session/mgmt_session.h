// Management-session lease and idle state machine.
//
// Pure logic with an injected millisecond clock: the firmware wraps it with a
// mutex because HTTP handlers and the 1 s tick run in different tasks, and host
// tests drive it with synthetic time.  Only one browser holds the edit lease;
// after a missed heartbeat the lease is reserved for the same client for a
// grace period.  An idle timeout of zero disables automatic mode exit.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PP_MGMT_CLIENT_ID_MAX 64
#define PP_MGMT_CLIENT_NAME_MAX 48
#define PP_MGMT_TOKEN_MAX 32

#define PP_MGMT_LEASE_TIMEOUT_MS (30 * 1000)
#define PP_MGMT_GRACE_MS (120 * 1000)

typedef enum {
    PP_MGMT_CLAIM_OK = 0,
    PP_MGMT_CLAIM_OCCUPIED,
    PP_MGMT_CLAIM_INVALID,
    PP_MGMT_CLAIM_NOT_RUNNING,
} pp_mgmt_claim_result_t;

typedef struct {
    bool running;
    int64_t started_ms;
    int64_t idle_deadline_ms;
    int64_t idle_timeout_ms;
    int64_t lease_timeout_ms;
    int64_t grace_ms;

    bool lease_active;
    char client_id[PP_MGMT_CLIENT_ID_MAX + 1];
    char client_name[PP_MGMT_CLIENT_NAME_MAX + 1];
    char token[PP_MGMT_TOKEN_MAX + 1];
    int64_t last_seen_ms;
    int64_t lease_deadline_ms;

    bool reserved;
    char reserved_client_id[PP_MGMT_CLIENT_ID_MAX + 1];
    int64_t reserved_until_ms;

    int long_ops;
} pp_mgmt_session_t;

typedef struct {
    bool running;
    bool occupied;
    bool is_editor;
    bool reserved_for_me;
    char client_name[PP_MGMT_CLIENT_NAME_MAX + 1];
    int grace_remaining_s;
    int idle_remaining_s;
    int long_ops;
} pp_mgmt_status_t;

// idle_timeout_ms == 0 keeps the management mode running until the caller
// stops it (the device's long-press OK); the lease timers still apply.
void pp_mgmt_session_start(pp_mgmt_session_t *session, int64_t now_ms,
                           int64_t idle_timeout_ms, int64_t lease_timeout_ms,
                           int64_t grace_ms);
void pp_mgmt_session_stop(pp_mgmt_session_t *session);

pp_mgmt_claim_result_t pp_mgmt_session_claim(pp_mgmt_session_t *session,
                                             int64_t now_ms,
                                             const char *client_id,
                                             const char *client_name,
                                             const char *new_token,
                                             char *out_token,
                                             size_t out_size);

// Refresh the lease and the idle deadline.  Returns false for an invalid,
// expired, or foreign token.
bool pp_mgmt_session_heartbeat(pp_mgmt_session_t *session, int64_t now_ms,
                               const char *token);
bool pp_mgmt_session_is_editor(const pp_mgmt_session_t *session,
                               const char *token);
bool pp_mgmt_session_release(pp_mgmt_session_t *session, int64_t now_ms,
                             const char *token);

// Uploads and other long operations pause the idle countdown and keep the
// current lease alive while they run.
bool pp_mgmt_session_begin_long_op(pp_mgmt_session_t *session,
                                   const char *token);
bool pp_mgmt_session_end_long_op(pp_mgmt_session_t *session,
                                 const char *token);

// Returns true when the management mode should exit (positive idle timeout
// only).  A zero timeout never returns true.
bool pp_mgmt_session_tick(pp_mgmt_session_t *session, int64_t now_ms);

void pp_mgmt_session_status(const pp_mgmt_session_t *session, int64_t now_ms,
                            const char *client_id, const char *token,
                            pp_mgmt_status_t *status);

int pp_mgmt_session_idle_remaining_s(const pp_mgmt_session_t *session,
                                     int64_t now_ms);
