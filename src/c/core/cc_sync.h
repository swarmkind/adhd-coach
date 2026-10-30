#pragma once
// Phone sync: frames that pack several records into one AppMessage byte array, the small
// records only sync uses, and the pure steps of applying a schedule from the phone.
#include "cc_types.h"
#include "cc_state.h"

enum { CC_T_ITEM = 1, CC_T_STEP, CC_T_SETTINGS, CC_T_LOG, CC_T_HIST, CC_T_NOTE_HEAD, CC_T_NOTE_PART,
       CC_T_TOMBS, CC_T_RENAME, CC_T_DELETE };
#define CC_FRAME_HDR 3  // [type u8][len u16]

int cc_frame_put(uint8_t *buf, int cap, int pos, uint8_t type, const uint8_t *rec, int len);  // new pos, -1 if it doesn't fit
// Next pos, 0 at the end, -1 if the buffer is malformed.
int cc_frame_next(const uint8_t *buf, int len, int pos, uint8_t *type, const uint8_t **rec, int *rec_len);

int cc_pack_step_at(uint16_t item, uint8_t index, const CcStep *s, uint8_t *buf, int len);  // [item u16][index u8] + step; index < CC_ITEM_STEPS
bool cc_unpack_step_at(const uint8_t *buf, int len, uint16_t *item, uint8_t *index, CcStep *s);
int cc_pack_settings_at(const CcSettings *c, uint32_t utc, uint8_t *buf, int len);  // [utc u32] + settings
bool cc_unpack_settings_at(const uint8_t *buf, int len, CcSettings *c, uint32_t *utc);
int cc_pack_tombs(const uint32_t *ids, int n, uint8_t *buf, int len);  // [n u8][id u32]...
int cc_unpack_tombs(const uint8_t *buf, int len, uint32_t *ids, int max);  // count, or -1
int cc_pack_rename(uint32_t id, const char *name, uint8_t *buf, int len);  // name cut to CC_NAME_MAX
bool cc_unpack_rename(const uint8_t *buf, int len, uint32_t *id, char name[CC_NAME_MAX]);

typedef struct { uint32_t rev, phone_id, settings_utc, last_sync_utc; } CcSync;
int cc_pack_sync(const CcSync *s, uint8_t *buf, int len);
bool cc_unpack_sync(const uint8_t *buf, int len, CcSync *s);

// The watch asks the phone for a sync after a change: soon (tries 0), then later while none comes.
uint32_t cc_ask_delay_ms(int tries);

int cc_day_prune(CcDay *d, const CcItem items[], int n);  // drops states whose item is gone; returns how many
bool cc_stage_complete(const CcItem *items, int n, const uint64_t *step_mask);  // every item has all its steps and alternatives
