// Personal profile model.  Text lives in profile.json; image presence is
// derived from the image slots on LittleFS so the JSON cannot reference a
// missing or stale file.
//
// Contacts are a generic list instead of fixed Weibo/WeChat fields.  Each entry
// has a display name for the platform, the account on that platform, and an
// optional link.  The device renders a QR code from the link when it is set.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PP_PROFILE_VERSION 2
#define PP_PROFILE_NAME_CP 24
#define PP_PROFILE_TAGLINE_CP 60
#define PP_PROFILE_PLATFORM_ID_CP 40
#define PP_PROFILE_CONTACT_MAX 8
#define PP_PROFILE_CONTACT_PLATFORM_CP 16
#define PP_PROFILE_CONTACT_ACCOUNT_CP 40
#define PP_PROFILE_URL_BYTES 512

typedef struct {
    char platform[PP_PROFILE_CONTACT_PLATFORM_CP * 4 + 1];
    char account[PP_PROFILE_CONTACT_ACCOUNT_CP * 4 + 1];
    char link[PP_PROFILE_URL_BYTES];
} pp_contact_t;

typedef struct {
    char name[PP_PROFILE_NAME_CP * 4 + 1];
    char tagline[PP_PROFILE_TAGLINE_CP * 4 + 1];
    char steam_id[PP_PROFILE_PLATFORM_ID_CP * 4 + 1];
    char switch_id[PP_PROFILE_PLATFORM_ID_CP * 4 + 1];
    char psn_id[PP_PROFILE_PLATFORM_ID_CP * 4 + 1];
    size_t contact_count;
    pp_contact_t contacts[PP_PROFILE_CONTACT_MAX];
} pp_profile_t;

typedef struct {
    bool avatar;
} pp_profile_images_t;

void pp_profile_defaults(pp_profile_t *profile);
void pp_profile_images_clear(pp_profile_images_t *images);

// Validates UTF-8, control bytes, code-point limits, and URL syntax.
bool pp_profile_validate(const pp_profile_t *profile, char *error,
                         size_t error_size);

// images may be NULL for the on-disk form.  When non-NULL the API form adds an
// "images" object.  The returned string is cJSON-allocated; free with
// cJSON_free().
char *pp_profile_to_json(const pp_profile_t *profile,
                         const pp_profile_images_t *images);

// Strict parse of the v1 schema: returns false and fills error on malformed
// input, wrong types, unknown versions, or fields outside their limits.
bool pp_profile_from_json(const char *json, pp_profile_t *profile, char *error,
                          size_t error_size);
