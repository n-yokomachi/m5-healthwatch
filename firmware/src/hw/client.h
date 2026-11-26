#pragma once
// HTTPS calls to the HealthWatch agent (AgentCore Runtime) with a Cognito access token.
// Everything here blocks; call it from the task that owns the conversation.
#include <cstddef>
#include <cstdint>

#include "hw/reply.h"

enum class NetErr { None, Wifi, Auth, Http, Timeout, Parse };

// POSTs a JSON body to the runtime and streams the response into sink.
// Waits up to 5 s for Wi-Fi, refreshes the token 60 s before it expires,
// and retries once with a new token on 401/403.
NetErr hwInvoke(const uint8_t* body, size_t len, ReplySink& sink, const char* sessionId);

// Fetches the Cognito token now unless a valid one is held, so the first request skips it.
NetErr hwPrefetchToken();

void hwNewSessionId(char out[40]);  // "hw-" + 32 random hex digits

// Writes {"type":"talk","audio":"<base64 of pcm>"} into out. Returns its length, 0 when cap is too small.
size_t hwBuildTalk(const int16_t* pcm, size_t samples, uint8_t* out, size_t cap);
