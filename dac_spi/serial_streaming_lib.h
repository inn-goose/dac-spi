#ifndef __serial_streaming_lib_h__
#define __serial_streaming_lib_h__

#include "core_mem.h"
#include "core_hsem.h"

namespace SerialStreamingLibrary {

/*
  High-throughput non-blocking serial receiver
  With packet timeout protection
  Writes directly to shared memory regions for dual-core operation

  Protocol v2:

  Common header (all packets):
    [0xAB][0xCD][0xEF][TYPE]
    TYPE = 0x01: metadata packet
    TYPE = 0x02: data packet

  Metadata packet (TYPE=0x01, sent once before streaming, 32 bytes after type):
    [0xAB][0xCD][0xEF][0x01][n_channels: u8][bits_per_sample: u8][sample_rate: u32 LE][packet_size: u16 LE][total_samples: u32 LE][debug: u8][reserved: 19 bytes]

  Data packet (TYPE=0x02, sent repeatedly):
    [0xAB][0xCD][0xEF][0x02][payload: packet_size * 2 bytes]
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
  WAIT_HEADER3,
  WAIT_TYPE,
  READ_META_FIELDS,
  WAIT_PAYLOAD
};

class StreamingParser {
public:
  StreamingParser()
    : _ring_buffer(), _state(WAIT_HEADER1), _last_byte_read_ts(0),
      _payload_index(0), _current_low_byte(0),
      _current_region_id(0), _region_acquired(false), _samples_dest(nullptr),
      _metadata_received(false),
      _meta_n_channels(0), _meta_bits_per_sample(0),
      _meta_sample_rate(0), _meta_packet_size(0), _meta_total_samples(0), _meta_debug(0),
      _meta_field_index(0) {
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
    // Release acquired region back to pool (without HSEM — M4 never sees partial data)
    if (_region_acquired && _region_available[_current_region_id]) {
      *_region_available[_current_region_id] = true;
    }
    _region_acquired = false;

    _state = WAIT_HEADER1;
    _payload_index = 0;
    _current_low_byte = 0;
    _samples_dest = nullptr;
    _meta_field_index = 0;
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
            _state = WAIT_HEADER3;
          } else if (b == _HEADER1) {
            // stay in WAIT_HEADER2: handles 0xAB 0xAB 0xCD re-sync
          } else {
            _state = WAIT_HEADER1;
          }
          break;

        case WAIT_HEADER3:
          if (b == _HEADER3) {
            _state = WAIT_TYPE;
          } else if (b == _HEADER1) {
            _state = WAIT_HEADER2;
          } else {
            _state = WAIT_HEADER1;
          }
          break;

        case WAIT_TYPE:
          if (b == _TYPE_METADATA) {
            // Reset streaming state for new metadata
            _metadata_received = false;
            _meta_field_index = 0;
            _state = READ_META_FIELDS;
          } else if (b == _TYPE_DATA) {
            if (!_metadata_received) {
              // No metadata yet, can't parse data
              _state = WAIT_HEADER1;
            } else {
              // Acquire region and prepare for payload
              _acquire_region();
              _region_acquired = true;

              SharedRegionHeader* header = get_region_header(_current_region_id);
              header->n_channels = _meta_n_channels;
              header->sample_rate = _meta_sample_rate;
              header->samples_count = _meta_packet_size;

              _samples_dest = get_region_samples(_current_region_id);
              _payload_index = 0;
              _current_low_byte = 0;
              _state = WAIT_PAYLOAD;
            }
          } else {
            // Invalid type, resync
            _state = WAIT_HEADER1;
          }
          break;

        case READ_META_FIELDS:
          _meta_buf[_meta_field_index++] = b;

          if (_meta_field_index >= _META_SIZE) {
            // Parse metadata fields from buffer
            _meta_n_channels = _meta_buf[0];
            _meta_bits_per_sample = _meta_buf[1];
            _meta_sample_rate = (uint32_t)_meta_buf[2]
                              | ((uint32_t)_meta_buf[3] << 8)
                              | ((uint32_t)_meta_buf[4] << 16)
                              | ((uint32_t)_meta_buf[5] << 24);
            _meta_packet_size = (uint16_t)_meta_buf[6]
                              | ((uint16_t)_meta_buf[7] << 8);
            _meta_total_samples = (uint32_t)_meta_buf[8]
                                | ((uint32_t)_meta_buf[9] << 8)
                                | ((uint32_t)_meta_buf[10] << 16)
                                | ((uint32_t)_meta_buf[11] << 24);
            _meta_debug = _meta_buf[12];
            // bytes 13–31 reserved

            // Validate
            if (_meta_n_channels == 0 || _meta_n_channels > _MAX_CHANNELS
                || _meta_packet_size == 0 || _meta_packet_size > MAX_SAMPLES_PER_REGION) {
              reset();
            } else {
              _metadata_received = true;
              // Ack metadata
              Serial.print("1");
              _state = WAIT_HEADER1;
            }
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

          if (_payload_index >= _meta_packet_size * 2) {
            _commit_region();
            _region_acquired = false;
            reset();
            // Ack to producer to start a new transmission
            Serial.print("1");
          }
          break;
      }
    }

    // Timeout: only meaningful when mid-packet and ring buffer is drained
    if (_state != WAIT_HEADER1 && _state != WAIT_HEADER2 && _state != WAIT_HEADER3) {
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
  static const uint8_t _HEADER3 = 0xEF;
  static const uint8_t _TYPE_METADATA = 0x01;
  static const uint8_t _TYPE_DATA = 0x02;
  static const uint8_t _META_SIZE = 32;  // bytes of metadata fields after type byte

  RingBuffer _ring_buffer;
  ParserState _state;
  unsigned long _last_byte_read_ts;

  uint32_t _payload_index;
  uint8_t _current_low_byte;

  // Region management
  int _current_region_id;
  bool _region_acquired;
  volatile int16_t* _samples_dest;
  volatile bool* _region_available[2];

  // Metadata
  bool _metadata_received;
  uint8_t _meta_n_channels;
  uint8_t _meta_bits_per_sample;
  uint32_t _meta_sample_rate;
  uint16_t _meta_packet_size;
  uint32_t _meta_total_samples;
  uint8_t _meta_debug;
  uint8_t _meta_buf[_META_SIZE];
  uint8_t _meta_field_index;

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
    size_t bytes_written = HEADER_SIZE + (_meta_packet_size * sizeof(int16_t));
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
