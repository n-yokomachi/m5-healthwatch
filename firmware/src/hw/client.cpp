#include "hw/client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <time.h>

#include <cstring>

#include "env_config.h"
#include "hw/chunked.h"

static const char COGNITO_URL[] = "https://cognito-idp.ap-northeast-1.amazonaws.com/";
static const uint32_t WIFI_WAIT_MS = 5000;
static const int32_t CONNECT_TIMEOUT_MS = 5000;
static const uint16_t RESPONSE_TIMEOUT_MS = 60000;  // also the longest gap between body bytes
static const uint32_t TOKEN_MARGIN_MS = 60000;
static const unsigned long HANDSHAKE_TIMEOUT_S = 15;  // NetworkClientSecure takes seconds (default 120)

// Both hosts chain to Amazon Root CA 1. The servers send it cross-signed by Starfield Services
// Root CA G2, so that root is trusted too. Checked with openssl s_client on 2026-11-26.
static const char ROOT_CA[] =
    // Amazon Root CA 1 (expires 2038-01-17)
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n"
    // Starfield Services Root Certificate Authority - G2 (expires 2037-12-31)
    "-----BEGIN CERTIFICATE-----\n"
    "MIID7zCCAtegAwIBAgIBADANBgkqhkiG9w0BAQsFADCBmDELMAkGA1UEBhMCVVMx\n"
    "EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxJTAjBgNVBAoT\n"
    "HFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xOzA5BgNVBAMTMlN0YXJmaWVs\n"
    "ZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5\n"
    "MDkwMTAwMDAwMFoXDTM3MTIzMTIzNTk1OVowgZgxCzAJBgNVBAYTAlVTMRAwDgYD\n"
    "VQQIEwdBcml6b25hMRMwEQYDVQQHEwpTY290dHNkYWxlMSUwIwYDVQQKExxTdGFy\n"
    "ZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTswOQYDVQQDEzJTdGFyZmllbGQgU2Vy\n"
    "dmljZXMgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIwDQYJKoZI\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBANUMOsQq+U7i9b4Zl1+OiFOxHz/Lz58gE20p\n"
    "OsgPfTz3a3Y4Y9k2YKibXlwAgLIvWX/2h/klQ4bnaRtSmpDhcePYLQ1Ob/bISdm2\n"
    "8xpWriu2dBTrz/sm4xq6HZYuajtYlIlHVv8loJNwU4PahHQUw2eeBGg6345AWh1K\n"
    "Ts9DkTvnVtYAcMtS7nt9rjrnvDH5RfbCYM8TWQIrgMw0R9+53pBlbQLPLJGmpufe\n"
    "hRhJfGZOozptqbXuNC66DQO4M99H67FrjSXZm86B0UVGMpZwh94CDklDhbZsc7tk\n"
    "6mFBrMnUVN+HL8cisibMn1lUaJ/8viovxFUcdUBgF4UCVTmLfwUCAwEAAaNCMEAw\n"
    "DwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFJxfAN+q\n"
    "AdcwKziIorhtSpzyEZGDMA0GCSqGSIb3DQEBCwUAA4IBAQBLNqaEd2ndOxmfZyMI\n"
    "bw5hyf2E3F/YNoHN2BtBLZ9g3ccaaNnRbobhiCPPE95Dz+I0swSdHynVv/heyNXB\n"
    "ve6SbzJ08pGCL72CQnqtKrcgfU28elUSwhXqvfdqlS5sdJ/PHLTyxQGjhdByPq1z\n"
    "qwubdQxtRbeOlKyWN7Wg0I8VRw7j6IPdj/3vQQF3zCepYoUz8jcI73HPdwbeyBkd\n"
    "iEDPfUYd/x7H4c7/I9vG+o1VTqkC50cRRj70/b17KSa7qWFiNyi2LSr2EIZkyXCn\n"
    "0q23KXB56jzaYyWf/Wi3MOxw+3WKt21gZ7IeyLnp2KhvAotnDU0mV3HaIPzBSlCN\n"
    "sSi6\n"
    "-----END CERTIFICATE-----\n"
    ;

static String sToken;  // Cognito access token, empty until the first InitiateAuth
static uint32_t sTokenAt = 0;
static uint32_t sTokenTtlMs = 0;

static bool waitWifi() {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_WAIT_MS) return false;
    delay(100);
  }
  return true;
}

static bool tokenValid() {
  return sToken.length() > 0 && sTokenTtlMs > TOKEN_MARGIN_MS && millis() - sTokenAt < sTokenTtlMs - TOKEN_MARGIN_MS;
}

static NetErr requestToken() {
  sToken = "";
  JsonDocument req;
  req["AuthFlow"] = "USER_PASSWORD_AUTH";
  req["ClientId"] = HW_CLIENT_ID;
  req["AuthParameters"]["USERNAME"] = HW_USERNAME;
  req["AuthParameters"]["PASSWORD"] = HW_PASSWORD;
  String body;
  serializeJson(req, body);

  NetworkClientSecure tls;
  tls.setCACert(ROOT_CA);
  tls.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(RESPONSE_TIMEOUT_MS);
  if (!http.begin(tls, COGNITO_URL)) return NetErr::Auth;
  http.addHeader("Content-Type", "application/x-amz-json-1.1");
  http.addHeader("X-Amz-Target", "AWSCognitoIdentityProviderService.InitiateAuth");
  int code = http.POST(body);
  if (code == HTTPC_ERROR_READ_TIMEOUT) {
    http.end();
    return NetErr::Timeout;
  }
  if (code != 200) {
    printf("hw auth: http %d\n", code);
    http.end();
    return NetErr::Auth;
  }
  String resp = http.getString();
  http.end();

  JsonDocument filter;
  filter["AuthenticationResult"]["AccessToken"] = true;
  filter["AuthenticationResult"]["ExpiresIn"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) return NetErr::Auth;
  const char* token = doc["AuthenticationResult"]["AccessToken"] | "";
  uint32_t expiresIn = doc["AuthenticationResult"]["ExpiresIn"] | 0;
  if (!*token || expiresIn == 0) return NetErr::Auth;
  sToken = token;
  sTokenAt = millis();
  sTokenTtlMs = expiresIn * 1000;
#ifdef HW_DEBUG_BAD_TOKEN
  static bool broken = false;  // only the first token, so the 401/403 retry gets a good one
  if (!broken) {
    broken = true;
    // Change one character in the middle of the JWT payload (between the first and second '.').
    // Every payload character carries 6 full bits, so the signature no longer matches.
    // The last character of the signature would not do: its low bits are padding.
    int a = sToken.indexOf('.');
    int b = a < 0 ? -1 : sToken.indexOf('.', a + 1);
    if (b > a + 1) {
      int mid = (a + b) / 2;
      sToken.setCharAt(mid, sToken[mid] == 'A' ? 'B' : 'A');
    }
  }
#endif
  return NetErr::None;
}

// logs the time InitiateAuth took (TLS connection included); the token itself is never printed
static NetErr fetchToken() {
  uint32_t t0 = millis();
  NetErr e = requestToken();
  printf("hw net: token %d in %lu ms, t %lu\n", (int)e, millis() - t0, millis());
  fflush(stdout);
  return e;
}

struct BodyRead {
  NetErr err;
  size_t bytes;
};

// Reads the response body from the socket into sink. HTTPClient::writeToStream is not used:
// it reads the CRLF after each chunk with readBytes, which gives up at once (-11) when those
// two bytes have not been decrypted yet. Here a gap only ends the read after RESPONSE_TIMEOUT_MS.
// contentLength is ignored for a chunked body; -1 means the body runs until the server closes.
static BodyRead readBody(NetworkClientSecure& tls, bool chunked, int contentLength, ReplySink& sink) {
  static uint8_t buf[1024];  // only the conversation task reads replies
  ChunkedDecoder decoder(sink);
  size_t bytes = 0;
  uint32_t lastData = millis();
  for (;;) {
    if (chunked ? decoder.done() : contentLength >= 0 && bytes >= (size_t)contentLength) return {NetErr::None, bytes};
    int avail = tls.available();
    if (avail <= 0) {
      if (!tls.connected()) {
        // a body without Content-Length ends when the server closes; anything else was cut short
        return {!chunked && contentLength < 0 ? NetErr::None : NetErr::Http, bytes};
      }
      if (millis() - lastData >= RESPONSE_TIMEOUT_MS) return {NetErr::Timeout, bytes};
      delay(5);
      continue;
    }
    size_t want = (size_t)avail < sizeof(buf) ? (size_t)avail : sizeof(buf);
    if (!chunked && contentLength >= 0 && want > (size_t)contentLength - bytes) want = (size_t)contentLength - bytes;
    int n = tls.read(buf, want);
    if (n <= 0) continue;  // available() said there was data; check again
    lastData = millis();
    if (chunked) {
      decoder.feed(buf, n);
      bytes = decoder.bodyBytes();
      if (decoder.error()) return {NetErr::Http, bytes};
    } else {
      sink.feed(buf, n);
      bytes += n;
    }
  }
}

// Returns the HTTP status, or a negative HTTPC_ERROR_* when the request did not complete.
static int post(const uint8_t* body, size_t len, ReplySink& sink, const char* sessionId, NetErr& streamErr) {
  streamErr = NetErr::None;
  uint32_t t0 = millis();
  NetworkClientSecure tls;
  tls.setCACert(ROOT_CA);
  tls.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(RESPONSE_TIMEOUT_MS);
  if (!http.begin(tls, HW_RUNTIME_URL)) return HTTPC_ERROR_CONNECTION_REFUSED;
  http.addHeader("Authorization", "Bearer " + sToken);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Amzn-Bedrock-AgentCore-Runtime-Session-Id", sessionId);
  static const char* KEEP_HEADERS[] = {"Transfer-Encoding"};
  http.collectHeaders(KEEP_HEADERS, 1);
  int code = http.POST(const_cast<uint8_t*>(body), len);
  bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
  uint32_t t1 = millis();
  // "in" covers the TLS connection, sending the body and waiting for the response headers
  printf("hw net: post %d in %lu ms, body %u bytes, t %lu, int free %u, int min %u\n", code, t1 - t0, (unsigned)len, t1, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  fflush(stdout);
  if (code == 200) {
    BodyRead r = readBody(tls, chunked, http.getSize(), sink);
    printf("hw net: stream %d in %lu ms, body %u bytes, chunked %d, t %lu, int free %u, finish %d, samples %u, head %u, trunc %d\n", (int)r.err, millis() - t1, (unsigned)r.bytes, (int)chunked, millis(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)sink.finish(), (unsigned)sink.samples(), (unsigned)sink.head().size(), (int)sink.truncated());
    fflush(stdout);
    streamErr = r.err;
  }
  http.end();
  return code;
}

NetErr hwInvoke(const uint8_t* body, size_t len, ReplySink& sink, const char* sessionId) {
  if (!waitWifi()) return NetErr::Wifi;
  if (!tokenValid()) {
    NetErr e = fetchToken();
    if (e != NetErr::None) return e;
  }
  for (int attempt = 0;; attempt++) {
    sink.reset();
    NetErr streamErr;
    int code = post(body, len, sink, sessionId, streamErr);
    if ((code == 401 || code == 403) && attempt == 0) {
      NetErr e = fetchToken();
      if (e != NetErr::None) return e;
      continue;
    }
    if (code == HTTPC_ERROR_READ_TIMEOUT) return NetErr::Timeout;
    if (code != 200) {
      printf("hw invoke: http %d\n", code);
      return NetErr::Http;
    }
    if (streamErr != NetErr::None) return streamErr;
    return sink.finish() ? NetErr::None : NetErr::Parse;
  }
}

NetErr hwPrefetchToken() {
  if (WiFi.status() != WL_CONNECTED) return NetErr::Wifi;
  return tokenValid() ? NetErr::None : fetchToken();
}

void hwNewSessionId(char out[40]) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  memcpy(out, "hw-", 3);
  for (int w = 0; w < 4; w++) {
    uint32_t r = esp_random();
    for (int i = 0; i < 8; i++) out[3 + w * 8 + i] = HEX_DIGITS[(r >> (i * 4)) & 0xF];
  }
  out[35] = '\0';
}

size_t hwBuildTalk(const int16_t* pcm, size_t samples, uint8_t* out, size_t cap) {
  static const char PRE[] = "{\"type\":\"talk\",\"audio\":\"";
  static const char POST[] = "\"}";
  const size_t preLen = sizeof(PRE) - 1;
  const size_t bytes = samples * sizeof(int16_t);
  const size_t b64Len = (bytes + 2) / 3 * 4;
  if (cap < preLen + b64Len + sizeof(POST)) return 0;  // sizeof(POST) also covers mbedtls's NUL
  memcpy(out, PRE, preLen);
  size_t written = 0;
  // ESP32 is little-endian, so the samples are already 16-bit LE PCM bytes
  if (mbedtls_base64_encode(out + preLen, cap - preLen, &written, reinterpret_cast<const uint8_t*>(pcm), bytes) != 0)
    return 0;
  memcpy(out + preLen + written, POST, sizeof(POST) - 1);
  return preLen + written + sizeof(POST) - 1;
}
