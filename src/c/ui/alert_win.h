#pragma once
// Full-screen alert for a due item or a heads-up. One at a time; later fires queue behind it.
#include "../core/cc_fires.h"
#include "../core/cc_state.h"

typedef void (*AlertDone)(const CcFire *fire, CcAction action);

void alert_win_push(const CcFire *fire, AlertDone done);
