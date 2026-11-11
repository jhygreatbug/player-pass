#include "data/profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/text_utf8.h"
#include "cJSON.h"
#include "data/data_validate.h"

#define PP_PROFILE_VERSION_LEGACY 1
#define PP_PROFILE_LEGACY_WEIBO_ACCOUNT_CP 40
#define PP_PROFILE_LEGACY_WECHAT_NAME_CP 24
#define PP_PROFILE_LEGACY_WECHAT_ID_CP 40

static bool get_string(const cJSON *parent, const char *key, char *dst,
                       size_t dst_size, size_t max_codepoints, bool required,
                       const char *field, char *error, size_t error_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item || cJSON_IsNull(item)) {
        if (required) {
            pp_set_error(error, error_size, "missing field: %s", field);
            return false;
        }
        dst[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item)) {
        pp_set_error(error, error_size, "%s must be a string", field);
        return false;
    }
    const pp_text_result_t result =
        pp_text_copy(dst, dst_size, item->valuestring, max_codepoints);
    if (result == PP_TEXT_INVALID_UTF8) {
        pp_set_error(error, error_size, "%s is not valid UTF-8", field);
        return false;
    }
    if (result == PP_TEXT_TOO_LONG) {
        pp_set_error(error, error_size, "%s exceeds %u characters", field,
                     (unsigned)max_codepoints);
        return false;
    }
    if (result != PP_TEXT_OK) {
        pp_set_error(error, error_size, "%s does not fit its buffer", field);
        return false;
    }
    return true;
}

void pp_profile_defaults(pp_profile_t *profile)
{
    if (!profile) return;
    memset(profile, 0, sizeof(*profile));
}

void pp_profile_images_clear(pp_profile_images_t *images)
{
    if (!images) return;
    memset(images, 0, sizeof(*images));
}

bool pp_profile_validate(const pp_profile_t *profile, char *error,
                         size_t error_size)
{
    if (!profile) {
        pp_set_error(error, error_size, "profile is null");
        return false;
    }
    if (!pp_field_valid(profile->name, PP_PROFILE_NAME_CP, false)) {
        pp_set_error(error, error_size, "name is invalid or too long");
        return false;
    }
    if (!pp_field_valid(profile->tagline, PP_PROFILE_TAGLINE_CP, true)) {
        pp_set_error(error, error_size, "tagline is invalid or too long");
        return false;
    }
    const struct {
        const char *value;
        const char *name;
        size_t limit;
    } fields[] = {
        { profile->steam_id, "steam_id", PP_PROFILE_PLATFORM_ID_CP },
        { profile->switch_id, "switch_id", PP_PROFILE_PLATFORM_ID_CP },
        { profile->psn_id, "psn_id", PP_PROFILE_PLATFORM_ID_CP },
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (!pp_field_valid(fields[i].value, fields[i].limit, false)) {
            pp_set_error(error, error_size, "%s is invalid or too long",
                         fields[i].name);
            return false;
        }
    }
    if (profile->contact_count > PP_PROFILE_CONTACT_MAX) {
        pp_set_error(error, error_size, "contacts exceeds %u entries",
                     (unsigned)PP_PROFILE_CONTACT_MAX);
        return false;
    }
    for (size_t i = 0; i < profile->contact_count; ++i) {
        const pp_contact_t *contact = &profile->contacts[i];
        if (contact->platform[0] == '\0' ||
            !pp_field_valid(contact->platform,
                            PP_PROFILE_CONTACT_PLATFORM_CP, false)) {
            pp_set_error(error, error_size,
                         "contacts[%u].platform is invalid or too long",
                         (unsigned)i);
            return false;
        }
        if (!pp_field_valid(contact->account, PP_PROFILE_CONTACT_ACCOUNT_CP,
                            false)) {
            pp_set_error(error, error_size,
                         "contacts[%u].account is invalid or too long",
                         (unsigned)i);
            return false;
        }
        if (!pp_optional_url_valid(contact->link)) {
            pp_set_error(error, error_size,
                         "contacts[%u].link must be an http(s) URL without "
                         "spaces",
                         (unsigned)i);
            return false;
        }
    }
    return true;
}

static void add_string(cJSON *root, const char *key, const char *value)
{
    cJSON_AddStringToObject(root, key, value ? value : "");
}

static bool add_contacts(cJSON *root, const pp_profile_t *profile)
{
    cJSON *contacts = cJSON_AddArrayToObject(root, "contacts");
    if (!contacts) return false;
    for (size_t i = 0; i < profile->contact_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) return false;
        add_string(item, "platform", profile->contacts[i].platform);
        add_string(item, "account", profile->contacts[i].account);
        add_string(item, "link", profile->contacts[i].link);
        cJSON_AddItemToArray(contacts, item);
    }
    return true;
}

char *pp_profile_to_json(const pp_profile_t *profile,
                         const pp_profile_images_t *images)
{
    if (!profile) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "version", PP_PROFILE_VERSION);
    add_string(root, "name", profile->name);
    add_string(root, "tagline", profile->tagline);

    cJSON *platforms = cJSON_AddObjectToObject(root, "platforms");
    cJSON *steam = platforms ? cJSON_AddObjectToObject(platforms, "steam") : NULL;
    cJSON *switch_ = platforms ? cJSON_AddObjectToObject(platforms, "switch") : NULL;
    cJSON *psn = platforms ? cJSON_AddObjectToObject(platforms, "playstation") : NULL;
    if (!platforms || !steam || !switch_ || !psn) {
        cJSON_Delete(root);
        return NULL;
    }
    add_string(steam, "id", profile->steam_id);
    add_string(switch_, "id", profile->switch_id);
    add_string(psn, "id", profile->psn_id);

    if (!add_contacts(root, profile)) {
        cJSON_Delete(root);
        return NULL;
    }

    if (images) {
        cJSON *flags = cJSON_AddObjectToObject(root, "images");
        if (!flags) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddBoolToObject(flags, "avatar", images->avatar);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static bool parse_contacts(const cJSON *root, pp_profile_t *profile, char *error,
                           size_t error_size)
{
    const cJSON *contacts = cJSON_GetObjectItemCaseSensitive(root, "contacts");
    if (!contacts || cJSON_IsNull(contacts)) return true;
    if (!cJSON_IsArray(contacts)) {
        pp_set_error(error, error_size, "contacts must be an array");
        return false;
    }
    const int count = cJSON_GetArraySize(contacts);
    if (count > PP_PROFILE_CONTACT_MAX) {
        pp_set_error(error, error_size, "contacts exceeds %u entries",
                     (unsigned)PP_PROFILE_CONTACT_MAX);
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(contacts, i);
        if (!cJSON_IsObject(item)) {
            pp_set_error(error, error_size,
                         "contacts[%d] must be an object", i);
            return false;
        }
        pp_contact_t *contact = &profile->contacts[i];
        char field[40];
        snprintf(field, sizeof(field), "contacts[%d].platform", i);
        if (!get_string(item, "platform", contact->platform,
                        sizeof(contact->platform),
                        PP_PROFILE_CONTACT_PLATFORM_CP, true, field, error,
                        error_size)) {
            return false;
        }
        snprintf(field, sizeof(field), "contacts[%d].account", i);
        if (!get_string(item, "account", contact->account,
                        sizeof(contact->account),
                        PP_PROFILE_CONTACT_ACCOUNT_CP, false, field, error,
                        error_size)) {
            return false;
        }
        snprintf(field, sizeof(field), "contacts[%d].link", i);
        if (!get_string(item, "link", contact->link, sizeof(contact->link),
                        PP_PROFILE_URL_BYTES - 1, false, field, error,
                        error_size)) {
            return false;
        }
        profile->contact_count = (size_t)i + 1;
    }
    return true;
}

static bool append_contact(pp_profile_t *profile, const char *platform,
                           const char *account, const char *link, char *error,
                           size_t error_size)
{
    if (profile->contact_count >= PP_PROFILE_CONTACT_MAX) {
        pp_set_error(error, error_size, "contacts exceeds %u entries",
                     (unsigned)PP_PROFILE_CONTACT_MAX);
        return false;
    }
    pp_contact_t *contact = &profile->contacts[profile->contact_count];
    memset(contact, 0, sizeof(*contact));
    snprintf(contact->platform, sizeof(contact->platform), "%s",
             platform ? platform : "");
    snprintf(contact->account, sizeof(contact->account), "%s",
             account ? account : "");
    snprintf(contact->link, sizeof(contact->link), "%s", link ? link : "");
    ++profile->contact_count;
    return true;
}

// v1 stored fixed contact.weibo / contact.wechat objects.  Convert them into
// generic entries so an upgrade keeps the text the user already entered.
static bool parse_legacy_contact(const cJSON *root, pp_profile_t *profile,
                                 char *error, size_t error_size)
{
    const cJSON *contact = cJSON_GetObjectItemCaseSensitive(root, "contact");
    if (!contact || cJSON_IsNull(contact)) return true;
    if (!cJSON_IsObject(contact)) {
        pp_set_error(error, error_size, "contact must be an object");
        return false;
    }

    const cJSON *weibo = cJSON_GetObjectItemCaseSensitive(contact, "weibo");
    if (weibo && !cJSON_IsNull(weibo)) {
        if (!cJSON_IsObject(weibo)) {
            pp_set_error(error, error_size,
                         "contact.weibo must be an object");
            return false;
        }
        char handle[PP_PROFILE_CONTACT_ACCOUNT_CP * 4 + 1];
        char url[PP_PROFILE_URL_BYTES];
        if (!get_string(weibo, "handle", handle, sizeof(handle),
                        PP_PROFILE_LEGACY_WEIBO_ACCOUNT_CP, false,
                        "contact.weibo.handle", error, error_size) ||
            !get_string(weibo, "url", url, sizeof(url),
                        PP_PROFILE_URL_BYTES - 1, false, "contact.weibo.url",
                        error, error_size)) {
            return false;
        }
        if ((handle[0] || url[0]) &&
            !append_contact(profile, "微博", handle, url, error,
                            error_size)) {
            return false;
        }
    }

    const cJSON *wechat = cJSON_GetObjectItemCaseSensitive(contact, "wechat");
    if (wechat && !cJSON_IsNull(wechat)) {
        if (!cJSON_IsObject(wechat)) {
            pp_set_error(error, error_size,
                         "contact.wechat must be an object");
            return false;
        }
        char name[PP_PROFILE_CONTACT_ACCOUNT_CP * 4 + 1];
        char id[PP_PROFILE_CONTACT_ACCOUNT_CP * 4 + 1];
        if (!get_string(wechat, "name", name, sizeof(name),
                        PP_PROFILE_LEGACY_WECHAT_NAME_CP, false,
                        "contact.wechat.name", error, error_size) ||
            !get_string(wechat, "id", id, sizeof(id),
                        PP_PROFILE_LEGACY_WECHAT_ID_CP, false,
                        "contact.wechat.id", error, error_size)) {
            return false;
        }
        if (name[0] &&
            !append_contact(profile, "微信", name, "", error, error_size)) {
            return false;
        }
        if (id[0] && !append_contact(profile, "微信号", id, "", error,
                                     error_size)) {
            return false;
        }
    }
    return true;
}

bool pp_profile_from_json(const char *json, pp_profile_t *profile, char *error,
                          size_t error_size)
{
    if (!json || !profile) {
        pp_set_error(error, error_size, "empty profile");
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        pp_set_error(error, error_size, "profile is not valid JSON");
        return false;
    }
    // The model is large (contacts carry links); keep it off the HTTP task
    // stack and only commit it after the whole record validates.
    pp_profile_t *parsed = calloc(1, sizeof(*parsed));
    if (!parsed) {
        pp_set_error(error, error_size, "out of memory");
        cJSON_Delete(root);
        return false;
    }
    bool ok = false;

    if (!cJSON_IsObject(root)) {
        pp_set_error(error, error_size, "profile root must be an object");
        goto done;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version)) {
        pp_set_error(error, error_size, "unsupported profile version");
        goto done;
    }
    const double version_value = version->valuedouble;
    if (version_value != (double)PP_PROFILE_VERSION &&
        version_value != (double)PP_PROFILE_VERSION_LEGACY) {
        pp_set_error(error, error_size, "unsupported profile version");
        goto done;
    }
    const int version_number = (int)version_value;
    if (!get_string(root, "name", parsed->name, sizeof(parsed->name),
                    PP_PROFILE_NAME_CP, false, "name", error, error_size) ||
        !get_string(root, "tagline", parsed->tagline, sizeof(parsed->tagline),
                    PP_PROFILE_TAGLINE_CP, false, "tagline", error,
                    error_size)) {
        goto done;
    }

    const cJSON *platforms =
        cJSON_GetObjectItemCaseSensitive(root, "platforms");
    if (platforms && !cJSON_IsObject(platforms)) {
        pp_set_error(error, error_size, "platforms must be an object");
        goto done;
    }
    if (platforms) {
        const struct {
            const char *key;
            char *dst;
            size_t size;
            const char *field;
        } platform_fields[] = {
            { "steam", parsed->steam_id, sizeof(parsed->steam_id), "steam.id" },
            { "switch", parsed->switch_id, sizeof(parsed->switch_id),
              "switch.id" },
            { "playstation", parsed->psn_id, sizeof(parsed->psn_id),
              "playstation.id" },
        };
        for (size_t i = 0;
             i < sizeof(platform_fields) / sizeof(platform_fields[0]); ++i) {
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(
                platforms, platform_fields[i].key);
            if (!item || cJSON_IsNull(item)) continue;
            if (!cJSON_IsObject(item)) {
                pp_set_error(error, error_size, "platforms.%s must be an object",
                             platform_fields[i].key);
                goto done;
            }
            if (!get_string(item, "id", platform_fields[i].dst,
                            platform_fields[i].size, PP_PROFILE_PLATFORM_ID_CP,
                            false, platform_fields[i].field, error,
                            error_size)) {
                goto done;
            }
        }
    }

    if (version_number == PP_PROFILE_VERSION_LEGACY) {
        if (!parse_legacy_contact(root, parsed, error, error_size)) goto done;
    } else if (!parse_contacts(root, parsed, error, error_size)) {
        goto done;
    }

    if (!pp_profile_validate(parsed, error, error_size)) goto done;
    *profile = *parsed;
    ok = true;

done:
    free(parsed);
    cJSON_Delete(root);
    return ok;
}
