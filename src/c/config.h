#pragma once

// 1 while developing: the sample schedule places items a few minutes after
// first launch so alerts fire quickly in the emulator. 0 for real use.
#define COACH_DEMO 0

// 1: canned dictation results instead of the phone's (the emulator has no dictation).
#ifndef COACH_FAKE_DICTATION
#define COACH_FAKE_DICTATION COACH_DEMO
#endif
