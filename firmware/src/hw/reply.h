#pragma once
// Reads the agent's HTTP response body (JSON whose last key is "audio", base64 PCM)
// chunk by chunk. No Arduino dependency so it can be tested on the host.
#include <cstddef>
#include <cstdint>
#include <string>

constexpr size_t REPLY_HEAD_MAX = 32 * 1024;

class ReplySink {
 public:
  ReplySink(int16_t* pcm, size_t maxSamples);
  void reset();
  size_t feed(const uint8_t* data, size_t len);  // always returns len
  bool finish() const;                           // true when the audio value was read to its closing quote
  const std::string& head() const { return head_; }
  size_t samples() const { return samples_; }
  bool truncated() const { return truncated_; }

 private:
  enum class State { Head, Audio, Done };
  void pushByte(uint8_t b);
  void decodeQuad();

  int16_t* pcm_;
  size_t maxSamples_;
  State state_ = State::Head;
  std::string head_;
  bool headOverflow_ = false;
  uint8_t quad_[4] = {0, 0, 0, 0};
  uint8_t quadLen_ = 0;
  size_t samples_ = 0;
  bool haveLow_ = false;
  uint8_t low_ = 0;
  bool truncated_ = false;
};
