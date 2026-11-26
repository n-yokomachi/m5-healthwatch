#pragma once
// Decodes an HTTP/1.1 chunked body (RFC 9112 section 7.1) from raw socket bytes and hands
// the body bytes to a ReplySink. No Arduino dependency so it can be tested on the host.
#include <cstddef>
#include <cstdint>

#include "hw/reply.h"

class ChunkedDecoder {
 public:
  explicit ChunkedDecoder(ReplySink& sink) : sink_(sink) {}
  void feed(const uint8_t* data, size_t len);  // bytes after done() or error() are ignored
  bool done() const { return state_ == State::Done; }    // read through the last chunk and the trailer
  bool error() const { return state_ == State::Error; }  // the bytes do not form a chunked body
  size_t bodyBytes() const { return bodyBytes_; }

 private:
  enum class State { Size, Ext, SizeLF, Data, DataCR, DataLF, TrailerStart, TrailerLine, TrailerLF, EndLF, Done, Error };

  ReplySink& sink_;
  State state_ = State::Size;
  uint32_t size_ = 0;  // the chunk size being read, then the data bytes left in the chunk
  uint8_t digits_ = 0;
  size_t bodyBytes_ = 0;
};
