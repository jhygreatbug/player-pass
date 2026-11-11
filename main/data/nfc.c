#include "data/nfc.h"

#include <string.h>

#include "app/text_utf8.h"
#include "cJSON.h"
#include "data/data_validate.h"

void pp_nfc_defaults(pp_nfc_t *nfc)
{
    if (!nfc) return;
    memset(nfc, 0, sizeof(*nfc));
    nfc->enabled = false;
    nfc->type = PP_NFC_TYPE_URL;
    (void)pp_text_copy(nfc->label, sizeof(nfc->label), "NFC 名片",
                       PP_NFC_LABEL_CP);
}

const char *pp_nfc_type_to_string(pp_nfc_type_t type)
{
    return type == PP_NFC_TYPE_TEXT ? "text" : "url";
}

bool pp_nfc_type_from_string(const char *text, pp_nfc_type_t *type)
{
    if (!text || !type) return false;
    if (strcmp(text, "url") == 0) {
        *type = PP_NFC_TYPE_URL;
        return true;
    }
    if (strcmp(text, "text") == 0) {
        *type = PP_NFC_TYPE_TEXT;
        return true;
    }
    return false;
}

bool pp_nfc_validate(const pp_nfc_t *nfc, char *error, size_t error_size)
{
    if (!nfc) {
        pp_set_error(error, error_size, "nfc is null");
        return false;
    }
    if (!pp_field_valid(nfc->label, PP_NFC_LABEL_CP, false) ||
        nfc->label[0] == '\0') {
        pp_set_error(error, error_size, "nfc label is invalid");
        return false;
    }
    if (!pp_field_valid(nfc->content, PP_NFC_CONTENT_CP,
                        nfc->type == PP_NFC_TYPE_TEXT)) {
        pp_set_error(error, error_size,
                     "nfc content is invalid or too long");
        return false;
    }
    if (nfc->type != PP_NFC_TYPE_URL && nfc->type != PP_NFC_TYPE_TEXT) {
        pp_set_error(error, error_size, "nfc type is invalid");
        return false;
    }
    if (nfc->enabled) {
        if (nfc->content[0] == '\0') {
            pp_set_error(error, error_size,
                         "enabled NFC entry needs content");
            return false;
        }
        if (nfc->type == PP_NFC_TYPE_URL && !pp_url_valid(nfc->content)) {
            pp_set_error(error, error_size, "nfc URL is not a valid http(s) URL");
            return false;
        }
    }
    return true;
}

char *pp_nfc_to_json(const pp_nfc_t *nfc)
{
    if (!nfc) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "version", PP_NFC_VERSION);
    cJSON_AddBoolToObject(root, "enabled", nfc->enabled);
    cJSON_AddStringToObject(root, "label", nfc->label);
    cJSON_AddStringToObject(root, "type", pp_nfc_type_to_string(nfc->type));
    cJSON_AddStringToObject(root, "content", nfc->content);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

bool pp_nfc_from_json(const char *json, pp_nfc_t *nfc, char *error,
                      size_t error_size)
{
    if (!json || !nfc) {
        pp_set_error(error, error_size, "empty NFC configuration");
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        pp_set_error(error, error_size, "NFC file is not valid JSON");
        return false;
    }
    pp_nfc_t parsed;
    pp_nfc_defaults(&parsed);
    bool ok = false;

    if (!cJSON_IsObject(root)) {
        pp_set_error(error, error_size, "NFC root must be an object");
        goto done;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valuedouble != PP_NFC_VERSION) {
        pp_set_error(error, error_size, "unsupported NFC version");
        goto done;
    }
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (enabled && !cJSON_IsBool(enabled)) {
        pp_set_error(error, error_size, "enabled must be a boolean");
        goto done;
    }
    parsed.enabled = cJSON_IsTrue(enabled);

    const cJSON *label = cJSON_GetObjectItemCaseSensitive(root, "label");
    if (label && !cJSON_IsNull(label)) {
        if (!cJSON_IsString(label) ||
            pp_text_copy(parsed.label, sizeof(parsed.label), label->valuestring,
                         PP_NFC_LABEL_CP) != PP_TEXT_OK) {
            pp_set_error(error, error_size, "NFC label is invalid or too long");
            goto done;
        }
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) ||
        !pp_nfc_type_from_string(type->valuestring, &parsed.type)) {
        pp_set_error(error, error_size, "NFC type must be url or text");
        goto done;
    }
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
    if (!content || cJSON_IsNull(content)) {
        parsed.content[0] = '\0';
    } else if (!cJSON_IsString(content) ||
               pp_text_copy(parsed.content, sizeof(parsed.content),
                            content->valuestring,
                            PP_NFC_CONTENT_CP) != PP_TEXT_OK) {
        pp_set_error(error, error_size, "NFC content is invalid or too long");
        goto done;
    }

    if (!pp_nfc_validate(&parsed, error, error_size)) goto done;
    *nfc = parsed;
    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}
