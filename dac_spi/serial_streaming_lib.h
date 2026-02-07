#ifndef __serial_streaming_lib_h__
#define __serial_streaming_lib_h__

#include "core_mem.h"
#include "core_hsem.h"

namespace SerialStreamingLibrary {

/*
  High-throughput non-blocking serial receiver
  With packet timeout protection
  Writes directly to shared memory regions for dual-core operation

  Packet format:
    [0xAB][0xCD][N_CHANNELS][LEN_LO][LEN_HI][PAYLOAD...]
    PAYLOAD = LEN * int16_t (little-endian)
*/

using namespace CoreMemory;
using namespace CoreHSEM;

// Ring Buffer

class RingBuffer {
public:
  RingBuffer()
    : _head(0), _tail(0) {}

  bool available() {
    return _head != _tail;
  }

  void push(uint8_t b) {
    uint32_t next = (_head + 1) & (_BUFFER_SIZE - 1);
    if (next != _tail) {
      _buffer[_head] = b;
      _head = next;
    }
  }

  bool pop(uint8_t* b) {
    if (!available()) return false;
    *b = _buffer[_tail];
    _tail = (_tail + 1) & (_BUFFER_SIZE - 1);
    return true;
  }

private:
  static const uint32_t _BUFFER_SIZE = 32 * 1024;  // power of 2
  uint8_t _buffer[_BUFFER_SIZE];
  volatile uint32_t _head;
  volatile uint32_t _tail;
};


// Packet Parser

enum ParserState {
  WAIT_HEADER1,
  WAIT_HEADER2,
  WAIT_NCHANNELS,
  WAIT_LEN_LO,
  WAIT_LEN_HI,
  WAIT_PAYLOAD
};

class StreamingParser {
public:
  StreamingParser()
    : _ring_buffer(), _state(WAIT_HEADER1), _last_byte_read_ts(0),
      _n_channels(0), _samples_count(0), _payload_index(0),
      _current_low_byte(0), _current_region_id(0), _samples_dest(nullptr) {
    _region_available[0] = nullptr;
    _region_available[1] = nullptr;
  }

  // Must be called before use to set region availability flags
  void init(volatile bool* region0_available, volatile bool* region1_available) {
    _region_available[0] = region0_available;
    _region_available[1] = region1_available;
  }

  void loop() {
    while (Serial.available()) {
      _ring_buffer.push(Serial.read());
    }
    parse();
  }

  void reset() {
    _state = WAIT_HEADER1;
    _n_channels = 0;
    _samples_count = 0;
    _payload_index = 0;
    _current_low_byte = 0;
    _samples_dest = nullptr;
  }

  void parse() {
    uint8_t b;

    while (_ring_buffer.pop(&b)) {
      _last_byte_read_ts = millis();

      switch (_state) {
        case WAIT_HEADER1:
          if (b == _HEADER1) {
            _state = WAIT_HEADER2;
          }
          break;

        case WAIT_HEADER2:
          if (b == _HEADER2) {
            _state = WAIT_NCHANNELS;
          } else if (b == _HEADER1) {
            // stay in WAIT_HEADER2: handles 0xAB 0xAB 0xCD re-sync
          } else {
            _state = WAIT_HEADER1;
          }
          break;

        case WAIT_NCHANNELS:
          _n_channels = b;
          if (_n_channels == 0 || _n_channels > _MAX_CHANNELS) {
            reset();
          } else {
            _state = WAIT_LEN_LO;
          }
          break;

        case WAIT_LEN_LO:
          _samples_count = b;
          _state = WAIT_LEN_HI;
          break;

        case WAIT_LEN_HI:
          _samples_count |= ((uint32_t)b << 8);

          if (_samples_count == 0 || _samples_count > MAX_SAMPLES_PER_REGION || (_samples_count % _n_channels) != 0) {
            reset();
          } else {
            // Acquire region (may block if both busy)
            _acquire_region();

            // Write header to shared memory
            SharedRegionHeader* header = get_region_header(_current_region_id);
            header->n_channels = _n_channels;
            header->samples_count = _samples_count;

            // Get samples destination pointer
            _samples_dest = get_region_samples(_current_region_id);

            _state = WAIT_PAYLOAD;
            _payload_index = 0;
            _current_low_byte = 0;
          }
          break;

        case WAIT_PAYLOAD:
          if (_payload_index & 1) {
            // odd byte (high): combine with stored low byte, write to shared memory
            _samples_dest[_payload_index >> 1] = (int16_t)(_current_low_byte | ((uint16_t)b << 8));
          } else {
            // even byte (low): stash it
            _current_low_byte = b;
          }
          _payload_index++;

          if (_payload_index >= _samples_count * 2) {
            _commit_region();
            reset();
            // ack to producer to start a new transmission
            // TODO: akward solution. rethink me
            Serial.print("1");
          }
          break;
      }
    }

    // Timeout: only meaningful when mid-packet and ring buffer is drained
    if (_state != WAIT_HEADER1 && _state != WAIT_HEADER2) {
      if (millis() - _last_byte_read_ts > _READ_TIMEOUT_MS) {
        reset();
      }
    }
  }

private:
  static const int _READ_TIMEOUT_MS = 10;  // adjust for baud rate
  static const uint8_t _MAX_CHANNELS = 2;
  static const uint8_t _HEADER1 = 0xAB;
  static const uint8_t _HEADER2 = 0xCD;

  RingBuffer _ring_buffer;
  ParserState _state;
  unsigned long _last_byte_read_ts;

  uint8_t _n_channels;
  uint32_t _samples_count;
  uint32_t _payload_index;
  uint8_t _current_low_byte;

  // Region management
  int _current_region_id;
  volatile int16_t* _samples_dest;
  volatile bool* _region_available[2];

  void _acquire_region() {
    // Try current region first
    if (*_region_available[_current_region_id]) {
      *_region_available[_current_region_id] = false;
      return;
    }

    // Try other region
    int other = 1 - _current_region_id;
    if (*_region_available[other]) {
      *_region_available[other] = false;
      _current_region_id = other;
      return;
    }

    // Both busy - pure spin wait
    while (!*_region_available[0] && !*_region_available[1]) {
      __NOP();
    }

    // One freed up - acquire it
    if (*_region_available[0]) {
      *_region_available[0] = false;
      _current_region_id = 0;
    } else {
      *_region_available[1] = false;
      _current_region_id = 1;
    }
  }

  void _commit_region() {

#if defined(CORE_CM7)
    // Clean cache for header + samples
    size_t bytes_written = HEADER_SIZE + (_samples_count * sizeof(int16_t));
    cleanDCache((void*)get_memory_region(_current_region_id), bytes_written);
#endif

    // Trigger HSEM to notify M4
    uint32_t hsem_id = (_current_region_id == 0) ? READ_HSEM_ID_0 : READ_HSEM_ID_1;
    hsem_trigger(hsem_id);

    // Switch to other region for next packet
    _current_region_id = 1 - _current_region_id;
  }
};


}  // SerialStreamingLibrary

#endif  // !__serial_streaming_lib_h__
