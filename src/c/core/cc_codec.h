#pragma once
// Pack/unpack records into byte buffers of at most 256 bytes (the Pebble
// persist value limit). Every record starts with a version byte.
#include "cc_types.h"
#include "cc_run.h"
#include "cc_state.h"
#include "cc_focus.h"

#define CC_CODEC_V1 1
#define CC_CODEC_V2 2  // settings, items, steps and the run; the rest are still V1
#define CC_DAY_CHUNK 16
#define CC_LOG_PER_CHUNK 20

void cc_utf8_trunc(char *s, size_t max_bytes_incl_nul);

int cc_pack_item(const CcItem *it, uint8_t *buf, int len);  // bytes written, or -1
bool cc_unpack_item(const uint8_t *buf, int len, CcItem *out);

int cc_pack_step(const CcStep *s, uint8_t *buf, int len);
bool cc_unpack_step(const uint8_t *buf, int len, CcStep *out);

int cc_pack_settings(const CcSettings *c, uint8_t *buf, int len);
bool cc_unpack_settings(const uint8_t *buf, int len, CcSettings *out);

// A day is stored as chunks of CC_DAY_CHUNK states; each chunk also carries day and n.
int cc_pack_day_chunk(const CcDay *d, int chunk, uint8_t *buf, int len);  // -1 if chunk is past n
bool cc_unpack_day_chunk(const uint8_t *buf, int len, CcDay *d);

int cc_pack_logs(const CcLogEntry *e, int n, uint8_t *buf, int len);
int cc_unpack_logs(const uint8_t *buf, int len, CcLogEntry *out, int max);  // entries read, or -1

int cc_pack_run(const CcRun *r, uint8_t *buf, int len);
bool cc_unpack_run(const uint8_t *buf, int len, CcRun *out);
int cc_pack_focus(const CcFocus *f, uint8_t *buf, int len);
bool cc_unpack_focus(const uint8_t *buf, int len, CcFocus *out);

// One day of outcomes (at most CC_MAX_ITEMS).
int cc_pack_history(int32_t day, const CcOutcome *o, int n, uint8_t *buf, int len);
int cc_unpack_history(const uint8_t *buf, int len, int32_t *day, CcOutcome *out, int max);  // outcomes read, or -1
