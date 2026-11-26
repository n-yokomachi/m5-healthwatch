#include "hw/chunked.h"

static int hexValue(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void ChunkedDecoder::feed(const uint8_t* data, size_t len) {
  size_t i = 0;
  while (i < len && state_ != State::Done && state_ != State::Error) {
    if (state_ == State::Data) {
      size_t n = len - i < size_ ? len - i : size_;
      sink_.feed(data + i, n);
      bodyBytes_ += n;
      size_ -= n;
      i += n;
      if (size_ == 0) state_ = State::DataCR;
      continue;
    }
    uint8_t c = data[i++];
    switch (state_) {
      case State::Size: {
        int v = hexValue(c);
        if (v >= 0) {
          if (++digits_ > 7) {  // up to 0xFFFFFFF; the reply is far smaller
            state_ = State::Error;
            break;
          }
          size_ = size_ * 16 + v;
        } else if (digits_ == 0) {
          state_ = State::Error;
        } else if (c == '\r') {
          state_ = State::SizeLF;
        } else if (c == ';' || c == ' ' || c == '\t') {
          state_ = State::Ext;  // chunk extensions are skipped
        } else {
          state_ = State::Error;
        }
        break;
      }
      case State::Ext:
        if (c == '\r') state_ = State::SizeLF;
        break;
      case State::SizeLF:
        if (c != '\n') {
          state_ = State::Error;
        } else if (size_ == 0) {
          state_ = State::TrailerStart;  // the last chunk
        } else {
          state_ = State::Data;
        }
        break;
      case State::DataCR:
        state_ = c == '\r' ? State::DataLF : State::Error;
        break;
      case State::DataLF:
        if (c == '\n') {
          state_ = State::Size;
          size_ = 0;
          digits_ = 0;
        } else {
          state_ = State::Error;
        }
        break;
      case State::TrailerStart:
        state_ = c == '\r' ? State::EndLF : State::TrailerLine;
        break;
      case State::TrailerLine:
        if (c == '\r') state_ = State::TrailerLF;
        break;
      case State::TrailerLF:
        state_ = c == '\n' ? State::TrailerStart : State::Error;
        break;
      case State::EndLF:
        state_ = c == '\n' ? State::Done : State::Error;
        break;
      default:
        break;
    }
  }
}
