
#pragma once

#include <string>
#include <mutex>

#include <boost/lexical_cast.hpp>

#include <fec.hh>

struct transfer_stats_t {
  transfer_stats_t(uint32_t _sequences = 0, uint32_t _blocks_in = 0, uint32_t _blocks_out = 0,
		   uint32_t _bytes_in = 0, uint32_t _bytes_out = 0, uint32_t _block_errors = 0,
		   uint32_t _sequence_errors = 0, uint32_t _inject_errors = 0,
		   float _encode_time = 0, float _send_time = 0, float _pkt_time = 0,
		   float _latency = 0, float _rssi= 0);
  uint32_t sequences;
  uint32_t blocks_in;
  uint32_t blocks_out;
  uint32_t sequence_errors;
  uint32_t block_errors;
  uint32_t inject_errors;
  uint32_t bytes_in;
  uint32_t bytes_out;
  float encode_time;
  float send_time;
  float pkt_time;
  float latency;
  float rssi;
};

class TransferStats {
public:

  TransferStats(const std::string &name);

  const std::string &name();

  void add(const FECDecoderStats &cur, const FECDecoderStats &prev);
  void add_rssi(int8_t rssi);
  void add_send_stats(uint32_t bytes, uint32_t nblocks, uint16_t inject_errors, uint32_t queue_size,
		      bool flush, float pkt_time);
  void add_encode_time(float t);
  void add_send_time(float t);
  void add_latency(uint8_t);
  transfer_stats_t get_stats();

  void update(const std::string &s);

  std::string serialize();

private:
  std::string m_name;
  float m_window;
  uint32_t m_seq;
  uint32_t m_blocks;
  uint32_t m_bytes;
  uint32_t m_block_errors;
  uint32_t m_seq_errors;
  uint32_t m_send_bytes;
  uint32_t m_send_blocks;
  uint32_t m_inject_errors;
  uint32_t m_flushes;
  float m_queue_size;
  float m_enc_time;
  float m_send_time;
  float m_pkt_time;
  float m_rssi;
  float m_latency;
  std::mutex m_mutex;
};
