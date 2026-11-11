#include <string.h>

#include "pp_test.h"
#include "session/mgmt_session.h"

static const int64_t IDLE = 300 * 1000;
static const int64_t LEASE = 30 * 1000;
static const int64_t GRACE = 120 * 1000;

static pp_mgmt_session_t fresh(int64_t now)
{
    pp_mgmt_session_t session;
    pp_mgmt_session_start(&session, now, IDLE, LEASE, GRACE);
    return session;
}

static void test_claim_and_status(void)
{
    pp_mgmt_session_t session = fresh(0);
    char token[PP_MGMT_TOKEN_MAX + 1] = { 0 };
    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                token, sizeof(token)) == PP_MGMT_CLAIM_OK);
    CHECK_STR_EQ(token, "tokenaaa");
    CHECK(pp_mgmt_session_is_editor(&session, "tokenaaa"));
    CHECK(!pp_mgmt_session_is_editor(&session, "tokenbbb"));

    pp_mgmt_status_t status;
    pp_mgmt_session_status(&session, 0, "client-a", "tokenaaa", &status);
    CHECK(status.running);
    CHECK(status.occupied);
    CHECK(status.is_editor);
    CHECK_STR_EQ(status.client_name, "iPhone");
    CHECK(status.idle_remaining_s == 300);

    CHECK(pp_mgmt_session_claim(&session, 1000, "client-b", "Android",
                                "tokenbbb", NULL, 0) ==
          PP_MGMT_CLAIM_OCCUPIED);

    // Invalid inputs never receive the lease.
    CHECK(pp_mgmt_session_claim(&session, 1000, "x", "bad", "tokenccc", NULL,
                                0) == PP_MGMT_CLAIM_INVALID);
    CHECK(pp_mgmt_session_claim(&session, 1000, "client-c", "bad", "short",
                                NULL, 0) == PP_MGMT_CLAIM_INVALID);

    // The same browser may reclaim after a reload and invalidates the old token.
    CHECK(pp_mgmt_session_claim(&session, 2000, "client-a", "iPhone",
                                "tokennew", NULL, 0) == PP_MGMT_CLAIM_OK);
    CHECK(!pp_mgmt_session_is_editor(&session, "tokenaaa"));
    CHECK(pp_mgmt_session_is_editor(&session, "tokennew"));
}

static void test_lease_expiry_and_grace(void)
{
    pp_mgmt_session_t session = fresh(0);
    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                NULL, 0) == PP_MGMT_CLAIM_OK);
    CHECK(pp_mgmt_session_heartbeat(&session, 10000, "tokenaaa"));
    CHECK(!pp_mgmt_session_heartbeat(&session, 40000, "tokenaaa"));

    CHECK(pp_mgmt_session_tick(&session, 40001) == false);
    CHECK(pp_mgmt_session_claim(&session, 40002, "client-b", "Android",
                                "tokenbbb", NULL, 0) ==
          PP_MGMT_CLAIM_OCCUPIED);

    pp_mgmt_status_t status;
    pp_mgmt_session_status(&session, 40002, "client-a", "", &status);
    CHECK(status.occupied);
    CHECK(status.reserved_for_me);
    CHECK(status.grace_remaining_s > 0);

    // The previous editor may reconnect during the grace period.
    CHECK(pp_mgmt_session_claim(&session, 50000, "client-a", "iPhone",
                                "tokenccc", NULL, 0) == PP_MGMT_CLAIM_OK);

    // Let the second lease expire and the grace window pass.
    (void)pp_mgmt_session_tick(&session, 81000);
    (void)pp_mgmt_session_tick(&session, 201001);
    CHECK(pp_mgmt_session_claim(&session, 201002, "client-b", "Android",
                                "tokenbbb", NULL, 0) == PP_MGMT_CLAIM_OK);
}

static void test_release(void)
{
    pp_mgmt_session_t session = fresh(0);
    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                NULL, 0) == PP_MGMT_CLAIM_OK);
    CHECK(!pp_mgmt_session_release(&session, 1000, "wrongtok"));
    CHECK(pp_mgmt_session_release(&session, 1000, "tokenaaa"));
    CHECK(pp_mgmt_session_claim(&session, 1001, "client-b", "Android",
                                "tokenbbb", NULL, 0) == PP_MGMT_CLAIM_OK);
}

static void test_idle_and_long_ops(void)
{
    pp_mgmt_session_t session = fresh(0);
    CHECK(pp_mgmt_session_tick(&session, IDLE - 1) == false);
    CHECK(pp_mgmt_session_tick(&session, IDLE) == true);
    CHECK(!session.running);

    session = fresh(0);
    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                NULL, 0) == PP_MGMT_CLAIM_OK);
    CHECK(pp_mgmt_session_begin_long_op(&session, "tokenaaa"));
    CHECK(!pp_mgmt_session_begin_long_op(&session, "wrongtok"));
    // Longer than both the lease and the idle timeout: the upload pauses both.
    CHECK(pp_mgmt_session_tick(&session, 300000) == false);
    CHECK(session.lease_active);
    CHECK(pp_mgmt_session_end_long_op(&session, "tokenaaa"));
    CHECK(pp_mgmt_session_tick(&session, 300001) == false);
    CHECK(pp_mgmt_session_tick(&session, 300001 + IDLE) == true);

    session = fresh(0);
    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                NULL, 0) == PP_MGMT_CLAIM_OK);
    CHECK(pp_mgmt_session_begin_long_op(&session, "tokenaaa"));
    // Heartbeats continue during most uploads; the lease must still refresh.
    CHECK(pp_mgmt_session_heartbeat(&session, 20000, "tokenaaa"));
    CHECK(pp_mgmt_session_end_long_op(&session, "tokenaaa"));
    CHECK(pp_mgmt_session_is_editor(&session, "tokenaaa"));
}

static void test_no_auto_exit(void)
{
    pp_mgmt_session_t session;
    pp_mgmt_session_start(&session, 0, 0, LEASE, GRACE);
    CHECK(pp_mgmt_session_tick(&session, 1000000) == false);
    CHECK(session.running);
    pp_mgmt_status_t status;
    pp_mgmt_session_status(&session, 1000000, "", "", &status);
    CHECK(status.idle_remaining_s == 0);

    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                NULL, 0) == PP_MGMT_CLAIM_OK);
    // The edit lease still expires and reserves the device for its grace
    // period, but the management mode itself never exits on idle.
    CHECK(pp_mgmt_session_tick(&session, LEASE + 1) == false);
    CHECK(session.running);
    CHECK(pp_mgmt_session_claim(&session, LEASE + 2, "client-b", "Android",
                                "tokenbbb", NULL, 0) ==
          PP_MGMT_CLAIM_OCCUPIED);
}

static void test_stopped(void)
{
    pp_mgmt_session_t session;
    pp_mgmt_session_stop(&session);
    CHECK(pp_mgmt_session_claim(&session, 0, "client-a", "iPhone", "tokenaaa",
                                NULL, 0) == PP_MGMT_CLAIM_NOT_RUNNING);
    CHECK(pp_mgmt_session_tick(&session, 0) == false);
}

int main(void)
{
    test_claim_and_status();
    test_lease_expiry_and_grace();
    test_release();
    test_idle_and_long_ops();
    test_no_auto_exit();
    test_stopped();
    return pp_test_result("test_mgmt_session");
}
