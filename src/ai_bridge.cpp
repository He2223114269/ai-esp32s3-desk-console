#include "ai_bridge.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

constexpr char kNvsNamespace[] = "odes_ai";
constexpr char kNvsUrl[] = "url";
constexpr char kNvsToken[] = "token";
constexpr size_t kUrlLength = 120;
constexpr size_t kTokenLength = 80;
constexpr size_t kMaxPrompt = 320;
constexpr size_t kChunkLength = 96;
constexpr UBaseType_t kResultQueueDepth = 10;
constexpr uint32_t kRequestTimeoutMs = 60000;

struct AiChunk {
  char text[kChunkLength];
  bool final;
};

Preferences prefs;
char endpointUrl[kUrlLength] = {};
char endpointToken[kTokenLength] = {};
bool endpointLoaded = false;

QueueHandle_t promptQueue = nullptr;
QueueHandle_t resultQueue = nullptr;
TaskHandle_t workerTask = nullptr;

volatile AiBridgeStatus status = AiBridgeStatus::kIdle;
char lastError[64] = {};

void setError(const char *message) {
  strlcpy(lastError, message, sizeof(lastError));
  status = AiBridgeStatus::kError;
  Serial.printf("[ai] error: %s\n", lastError);
}

void ensureEndpointLoaded() {
  if (endpointLoaded) return;
  prefs.begin(kNvsNamespace, false);
  prefs.getString(kNvsUrl, endpointUrl, kUrlLength);
  prefs.getString(kNvsToken, endpointToken, kTokenLength);
  prefs.end();
  endpointLoaded = true;
}

// Minimal JSON string escaping so user text can never break the request body.
size_t appendJsonEscaped(char *out, size_t outSize, const char *text) {
  size_t written = 0;
  for (const char *cursor = text; *cursor && written + 8 < outSize; ++cursor) {
    const char escape = *cursor == '\\' ? '\\' : (*cursor == '"' ? '"' : 0);
    if (escape) {
      out[written++] = '\\';
      out[written++] = escape;
    } else if (static_cast<unsigned char>(*cursor) < 0x20) {
      written += snprintf(out + written, outSize - written, "\\u%04x",
                           static_cast<unsigned char>(*cursor));
    } else {
      out[written++] = *cursor;
    }
  }
  out[written] = '\0';
  return written;
}

void publishChunk(const char *text, size_t length, bool final) {
  if (resultQueue == nullptr) return;
  AiChunk chunk = {};
  const size_t copyLength = length < sizeof(chunk.text) - 1
                                ? length
                                : sizeof(chunk.text) - 1;
  memcpy(chunk.text, text, copyLength);
  chunk.text[copyLength] = '\0';
  chunk.final = final;
  xQueueSend(resultQueue, &chunk, pdMS_TO_TICKS(500));
}

void replyOffline() {
  const char *message = "You are offline. Check Wi-Fi on the TOOLS page.";
  setError("offline");
  publishChunk(message, strlen(message), true);
}

void requestTask(void *argument) {
  (void)argument;
  char prompt[kMaxPrompt] = {};

  for (;;) {
    if (xQueueReceive(promptQueue, prompt, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    ensureEndpointLoaded();
    if (endpointUrl[0] == '\0') {
      const char *message = "AI endpoint is not configured yet.";
      setError("no endpoint set");
      publishChunk(message, strlen(message), true);
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      replyOffline();
      continue;
    }

    char escaped[kMaxPrompt * 6] = {};
    appendJsonEscaped(escaped, sizeof(escaped), prompt);
    char body[kMaxPrompt * 6 + 64] = {};
    snprintf(body, sizeof(body), "{\"message\":\"%s\"}", escaped);

    status = AiBridgeStatus::kThinking;
    lastError[0] = '\0';

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(kRequestTimeoutMs);
    http.setReuse(false);
    if (!http.begin(client, endpointUrl)) {
      const char *message = "The AI endpoint URL is invalid.";
      setError("bad url");
      publishChunk(message, strlen(message), true);
      continue;
    }
    http.addHeader("Content-Type", "application/json");
    if (endpointToken[0] != '\0') {
      char header[96];
      snprintf(header, sizeof(header), "Bearer %s", endpointToken);
      http.addHeader("Authorization", header);
    }

    const int code = http.POST(body);
    if (code != 200) {
      char message[96];
      snprintf(message, sizeof(message), "Request failed (HTTP %d).", code);
      setError(message);
      http.end();
      publishChunk(message, strlen(message), true);
      continue;
    }

    status = AiBridgeStatus::kAnswering;
    WiFiClient *stream = http.getStreamPtr();
    char chunk[kChunkLength] = {};
    size_t chunkLength = 0;
    uint32_t lastDataMs = millis();
    while (http.connected() && millis() - lastDataMs < kRequestTimeoutMs) {
      if (stream->available() == 0) {
        delay(4);
        continue;
      }
      lastDataMs = millis();
      const size_t room = sizeof(chunk) - 1 - chunkLength;
      const int read = stream->readBytes(chunk + chunkLength, room);
      if (read <= 0) continue;
      chunkLength += read;

      // Break on newlines so replies arrive sentence by sentence.
      char *newline = static_cast<char *>(memchr(chunk, '\n', chunkLength));
      if (newline == nullptr) {
        if (chunkLength + 1 >= sizeof(chunk)) {
          publishChunk(chunk, chunkLength, false);
          chunkLength = 0;
        }
        continue;
      }
      const size_t lineLength = static_cast<size_t>(newline - chunk);
      publishChunk(chunk, lineLength, false);
      const size_t rest = chunkLength - lineLength - 1;
      memmove(chunk, newline + 1, rest);
      chunkLength = rest;
    }
    if (chunkLength > 0) publishChunk(chunk, chunkLength, true);
    else publishChunk("", 0, true);
    http.end();
    status = AiBridgeStatus::kIdle;
  }
}

}  // namespace

void aiBridgeBegin() {
  ensureEndpointLoaded();
  promptQueue = xQueueCreate(1, kMaxPrompt);
  resultQueue = xQueueCreate(kResultQueueDepth, sizeof(AiChunk));
  if (promptQueue == nullptr || resultQueue == nullptr) {
    setError("alloc failed");
    return;
  }
  if (xTaskCreatePinnedToCore(requestTask, "ai_request", 8192, nullptr, 1,
                              &workerTask, 0) != pdPASS) {
    setError("task create failed");
    return;
  }
  Serial.printf("[ai] bridge ready, endpoint=%s\n",
                endpointUrl[0] ? endpointUrl : "(unset)");
}

void aiBridgeSubmit(const char *userText) {
  if (promptQueue == nullptr || userText == nullptr) return;
  if (status == AiBridgeStatus::kThinking ||
      status == AiBridgeStatus::kAnswering) {
    Serial.println("[ai] ignored: request in progress");
    return;
  }
  char prompt[kMaxPrompt] = {};
  strlcpy(prompt, userText, sizeof(prompt));
  if (xQueueSend(promptQueue, prompt, 0) != pdTRUE) {
    setError("busy");
    return;
  }
  status = AiBridgeStatus::kThinking;
  lastError[0] = '\0';
}

bool aiBridgePoll(char *out, size_t outSize, bool *final) {
  if (resultQueue == nullptr || out == nullptr || outSize == 0) return false;
  AiChunk chunk = {};
  if (xQueueReceive(resultQueue, &chunk, 0) != pdTRUE) return false;
  strlcpy(out, chunk.text, outSize);
  if (final) *final = chunk.final;
  return true;
}

void aiBridgeClearHistory() {
  if (resultQueue != nullptr) xQueueReset(resultQueue);
  status = AiBridgeStatus::kIdle;
}

AiBridgeStatus aiBridgeStatus() { return status; }

const char *aiBridgeLastError() { return lastError; }

bool aiBridgeConfigured() {
  ensureEndpointLoaded();
  return endpointUrl[0] != '\0';
}

void aiBridgeLoadEndpoint(char *url, size_t urlSize, char *token,
                          size_t tokenSize) {
  ensureEndpointLoaded();
  strlcpy(url, endpointUrl, urlSize);
  strlcpy(token, endpointToken, tokenSize);
}

bool aiBridgeSaveEndpoint(const char *url, const char *token) {
  prefs.begin(kNvsNamespace, false);
  prefs.putString(kNvsUrl, url);
  prefs.putString(kNvsToken, token);
  prefs.end();
  strlcpy(endpointUrl, url, kUrlLength);
  strlcpy(endpointToken, token, kTokenLength);
  endpointLoaded = true;
  Serial.println("[ai] endpoint saved");
  return true;
}
