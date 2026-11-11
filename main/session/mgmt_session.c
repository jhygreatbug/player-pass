#include "session/mgmt_session.h"

#include <string.h>

#include "app/text_utf8.h"

static bool token_valid(const char *token)
{
    return pp_text_is_ascii_id(token, 8, PP_MGMT_TOKEN_MAX);
}

static bool client_valid(const char *client_id)
{
    return pp_text_is_ascii_id(client_id, 4, PP_MGMT_CLIENT_ID_MAX);
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    const size_t length = strlen(src);
    const size_t copy = length < dst_size - 1 ? length : dst_size - 1;
    memcpy(dst, src, copy);
    dst[copy] = '\0';
}

static void clear_lease(pp_mgmt_session_t *session)
{
    session->lease_active = false;
    session->client_id[0] = '\0';
    session->client_name[0] = '\0';
    session->token[0] = '\0';
}

static void expire_lease(pp_mgmt_session_t *session)
{
    if (!session->lease_active) return;
    session->reserved = true;
    copy_string(session->reserved_client_id,
                sizeof(session->reserved_client_id), session->client_id);
    session->reserved_until_ms = session->lease_deadline_ms + session->grace_ms;
    clear_lease(session);
}

static void expire_reservation(pp_mgmt_session_t *session, int64_t now_ms)
{
    if (!session->reserved) return;
    if (now_ms < session->reserved_until_ms) return;
    session->reserved = false;
    session->reserved_client_id[0] = '\0';
    session->reserved_until_ms = 0;
}

static void note_activity(pp_mgmt_session_t *session, int64_t now_ms)
{
    session->idle_deadline_ms = now_ms + session->idle_timeout_ms;
}

void pp_mgmt_session_start(pp_mgmt_session_t *session, int64_t now_ms,
                           int64_t idle_timeout_ms, int64_t lease_timeout_ms,
                           int64_t grace_ms)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->running = true;
    session->started_ms = now_ms;
    session->idle_timeout_ms = idle_timeout_ms;
    session->lease_timeout_ms = lease_timeout_ms;
    session->grace_ms = grace_ms;
    session->idle_deadline_ms = now_ms + idle_timeout_ms;
}

void pp_mgmt_session_stop(pp_mgmt_session_t *session)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
}

pp_mgmt_claim_result_t pp_mgmt_session_claim(pp_mgmt_session_t *session,
                                             int64_t now_ms,
                                             const char *client_id,
                                             const char *client_name,
                                             const char *new_token,
                                             char *out_token,
                                             size_t out_size)
{
    if (!session || !session->running) return PP_MGMT_CLAIM_NOT_RUNNING;
    if (!client_valid(client_id) || !token_valid(new_token)) {
        return PP_MGMT_CLAIM_INVALID;
    }
    if (client_name && !pp_text_valid(client_name)) return PP_MGMT_CLAIM_INVALID;

    if (session->lease_active && now_ms >= session->lease_deadline_ms) {
        expire_lease(session);
    }
    expire_reservation(session, now_ms);

    const bool same_client = session->lease_active &&
                             strcmp(session->client_id, client_id) == 0;
    if (session->lease_active && !same_client) {
        return PP_MGMT_CLAIM_OCCUPIED;
    }
    if (!session->lease_active && session->reserved &&
        strcmp(session->reserved_client_id, client_id) != 0) {
        return PP_MGMT_CLAIM_OCCUPIED;
    }

    // The same browser may reclaim after a reload; issuing a fresh token
    // invalidates any stale page that still holds the old one.
    session->lease_active = true;
    copy_string(session->client_id, sizeof(session->client_id), client_id);
    copy_string(session->client_name, sizeof(session->client_name), client_name);
    copy_string(session->token, sizeof(session->token), new_token);
    session->last_seen_ms = now_ms;
    session->lease_deadline_ms = now_ms + session->lease_timeout_ms;
    session->reserved = false;
    session->reserved_client_id[0] = '\0';
    session->reserved_until_ms = 0;
    note_activity(session, now_ms);

    if (out_token && out_size > 0) {
        copy_string(out_token, out_size, new_token);
    }
    return PP_MGMT_CLAIM_OK;
}

bool pp_mgmt_session_is_editor(const pp_mgmt_session_t *session,
                               const char *token)
{
    if (!session || !session->running || !session->lease_active || !token) {
        return false;
    }
    return strcmp(session->token, token) == 0;
}

bool pp_mgmt_session_heartbeat(pp_mgmt_session_t *session, int64_t now_ms,
                               const char *token)
{
    if (!pp_mgmt_session_is_editor(session, token)) return false;
    if (now_ms >= session->lease_deadline_ms) {
        expire_lease(session);
        return false;
    }
    session->last_seen_ms = now_ms;
    session->lease_deadline_ms = now_ms + session->lease_timeout_ms;
    note_activity(session, now_ms);
    return true;
}

bool pp_mgmt_session_release(pp_mgmt_session_t *session, int64_t now_ms,
                             const char *token)
{
    if (!pp_mgmt_session_is_editor(session, token)) return false;
    clear_lease(session);
    session->reserved = false;
    session->reserved_client_id[0] = '\0';
    session->reserved_until_ms = 0;
    note_activity(session, now_ms);
    return true;
}

bool pp_mgmt_session_begin_long_op(pp_mgmt_session_t *session,
                                   const char *token)
{
    if (!pp_mgmt_session_is_editor(session, token)) return false;
    ++session->long_ops;
    return true;
}

bool pp_mgmt_session_end_long_op(pp_mgmt_session_t *session,
                                 const char *token)
{
    if (!session || session->long_ops <= 0) return false;
    if (!pp_mgmt_session_is_editor(session, token)) return false;
    --session->long_ops;
    return true;
}

bool pp_mgmt_session_tick(pp_mgmt_session_t *session, int64_t now_ms)
{
    if (!session || !session->running) return false;

    if (session->long_ops > 0) {
        // A browser cannot heartbeat while a large upload occupies its main
        // thread on some phones; pause both countdowns instead of dropping the
        // editor mid-transfer.
        session->idle_deadline_ms = now_ms + session->idle_timeout_ms;
        if (session->lease_active) {
            session->lease_deadline_ms = now_ms + session->lease_timeout_ms;
        }
    } else if (session->lease_active && now_ms >= session->lease_deadline_ms) {
        expire_lease(session);
    }

    expire_reservation(session, now_ms);

    if (session->idle_timeout_ms > 0 && now_ms >= session->idle_deadline_ms) {
        session->running = false;
        return true;
    }
    return false;
}

void pp_mgmt_session_status(const pp_mgmt_session_t *session, int64_t now_ms,
                            const char *client_id, const char *token,
                            pp_mgmt_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!session) return;
    status->running = session->running;
    status->is_editor = pp_mgmt_session_is_editor(session, token);
    status->long_ops = session->long_ops;
    status->occupied = session->lease_active ||
                       (session->reserved && now_ms < session->reserved_until_ms);
    if (session->lease_active) {
        copy_string(status->client_name, sizeof(status->client_name),
                    session->client_name);
    }
    if (session->reserved && now_ms < session->reserved_until_ms) {
        const int64_t remaining = session->reserved_until_ms - now_ms;
        status->grace_remaining_s = (int)((remaining + 999) / 1000);
        status->reserved_for_me =
            client_id && session->reserved_client_id[0] &&
            strcmp(session->reserved_client_id, client_id) == 0;
    }
    const int64_t idle = session->idle_deadline_ms - now_ms;
    status->idle_remaining_s = idle > 0 ? (int)((idle + 999) / 1000) : 0;
}

int pp_mgmt_session_idle_remaining_s(const pp_mgmt_session_t *session,
                                     int64_t now_ms)
{
    if (!session || !session->running) return 0;
    const int64_t idle = session->idle_deadline_ms - now_ms;
    return idle > 0 ? (int)((idle + 999) / 1000) : 0;
}
