#pragma once

#include <Arduino.h>

// Chat transport for the ODES assistant. The board never stores a model API
// key: it talks to a small proxy that runs on your PC and holds the key.

enum class AiBridgeStatus : uint8_t {
  kIdle,
  kThinking,
  kAnswering,
  kOffline,
  kError,
};

void aiBridgeBegin();

// Feed new text to the model. Safe to call from the LVGL event context.
void aiBridgeSubmit(const char* userText);

// Non-blocking pump. Returns true and fills `out` when a new piece of the
// assistant answer should be shown. `final` reports the last chunk.
bool aiBridgePoll(char *out, size_t outSize, bool *final);

void aiBridgeClearHistory();
AiBridgeStatus aiBridgeStatus();
const char *aiBridgeLastError();
bool aiBridgeConfigured();

void aiBridgeLoadEndpoint(char *url, size_t urlSize, char *token,
                          size_t tokenSize);
bool aiBridgeSaveEndpoint(const char *url, const char *token);

