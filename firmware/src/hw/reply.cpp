#include "hw/reply.h"

static const char MARKER[] = ",\"audio\":\"";
static const size_t MARKER_LEN = sizeof(MARKER) - 1;

static int b64Value(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

ReplySink::ReplySink(int16_t* pcm, size_t maxSamples) : pcm_(pcm), maxSamples_(maxSamples) {}

void ReplySink::reset() {
  state_ = State::Head;
  head_.clear();
  headOverflow_ = false;
  quadLen_ = 0;
  samples_ = 0;
  haveLow_ = false;
  truncated_ = false;
}

bool ReplySink::finish() const { return state_ == State::Done && !headOverflow_; }

void ReplySink::pushByte(uint8_t b) {
  if (!haveLow_) {
    low_ = b;
    haveLow_ = true;
    return;
  }
  haveLow_ = false;
  if (samples_ >= maxSamples_) {
    truncated_ = true;
    return;
  }
  pcm_[samples_++] = static_cast<int16_t>(static_cast<uint16_t>(low_) | (static_cast<uint16_t>(b) << 8));
}

// quad_ holds 2-4 base64 values; padding was already stripped by the caller
void ReplySink::decodeQuad() {
  uint32_t v = (quad_[0] << 18) | (quad_[1] << 12) | (quad_[2] << 6) | quad_[3];
  pushByte((v >> 16) & 0xFF);
  if (quadLen_ > 2) pushByte((v >> 8) & 0xFF);
  if (quadLen_ > 3) pushByte(v & 0xFF);
}

size_t ReplySink::feed(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    if (state_ == State::Head) {
      if (headOverflow_) continue;
      if (head_.size() >= REPLY_HEAD_MAX) {
        headOverflow_ = true;
        continue;
      }
      head_.push_back(static_cast<char>(c));
      if (head_.size() >= MARKER_LEN && head_.compare(head_.size() - MARKER_LEN, MARKER_LEN, MARKER) == 0) {
        head_.resize(head_.size() - MARKER_LEN);
        head_.push_back('}');
        state_ = State::Audio;
      }
    } else if (state_ == State::Audio) {
      if (c == '"') {
        if (quadLen_ >= 2) decodeQuad();  // padding omitted
        quadLen_ = 0;
        state_ = State::Done;
      } else if (c == '=') {
        if (quadLen_ >= 2) decodeQuad();
        quadLen_ = 0;
      } else {
        int v = b64Value(c);
        if (v < 0) continue;
        quad_[quadLen_++] = static_cast<uint8_t>(v);
        if (quadLen_ == 4) {
          decodeQuad();
          quadLen_ = 0;
        }
      }
    }
  }
  return len;
}
