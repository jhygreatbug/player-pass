// NFC entry configuration.
//
// The product's NTAG213 is a passive tag with no MCU interface, so the firmware
// cannot rewrite it.  This record is the intended payload for that tag: the
// admin edits it, the device shows it (and a QR copy), and the user writes it to
// the tag with a phone.  Nothing here claims on-device NFC access.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PP_NFC_VERSION 1
#define PP_NFC_LABEL_CP 16
#define PP_NFC_CONTENT_CP 160
#define PP_NFC_CONTENT_BYTES 512

typedef enum {
    PP_NFC_TYPE_URL = 0,
    PP_NFC_TYPE_TEXT = 1,
} pp_nfc_type_t;

typedef struct {
    bool enabled;
    char label[PP_NFC_LABEL_CP * 4 + 1];
    pp_nfc_type_t type;
    char content[PP_NFC_CONTENT_BYTES];
} pp_nfc_t;

void pp_nfc_defaults(pp_nfc_t *nfc);
const char *pp_nfc_type_to_string(pp_nfc_type_t type);
bool pp_nfc_type_from_string(const char *text, pp_nfc_type_t *type);
bool pp_nfc_validate(const pp_nfc_t *nfc, char *error, size_t error_size);
char *pp_nfc_to_json(const pp_nfc_t *nfc);
bool pp_nfc_from_json(const char *json, pp_nfc_t *nfc, char *error,
                      size_t error_size);
