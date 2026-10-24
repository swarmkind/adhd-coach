#pragma once
#include "../core/cc_types.h"

// Fills the model with the sample schedule and saves it. Called by store_load when no
// schedule exists. With COACH_DEMO the routine and the dentist task land minutes after `now`.
void sample_load(cc_sec now);
