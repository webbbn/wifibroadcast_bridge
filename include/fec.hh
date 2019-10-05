#ifndef FEC_ENCODER_HH
#define FEC_ENCODER_HH

#include <stdint.h>
#include <memory.h>

#include <vector>
#include <memory>
#include <iostream>
#include <set>
#include <queue>

#include <fec.h>

typedef enum {
  FEC_PARTIAL,
  FEC_COMPLETE,
  FEC_ERROR
} FECStatus;

struct __attribute__((__packed__)) FECHeader {
  FECHeader() : seq_num(0), block(0), n_blocks(0), n_fec_blocks(0), length() {}
  uint8_t seq_num;
  uint8_t block;
  uint8_t n_blocks;
  uint8_t n_fec_blocks;
  uint16_t length;
};

class FECBlock {
public:
  FECBlock(uint8_t seq_num, uint8_t block, uint8_t nblocks, uint8_t nfec_blocks,
	   uint16_t data_length) :
    m_data(data_length + sizeof(FECHeader)) {
    FECHeader *h = header();
    h->seq_num = seq_num;
    h->block = block;
    h->n_blocks = nblocks;
    h->n_fec_blocks = nfec_blocks;
    h->length = data_length;
    m_pkt_length = data_length + sizeof(FECHeader);
  }
  FECBlock(const uint8_t *buf, uint16_t pkt_length) {
    m_data.resize(pkt_length);
    std::copy(buf, buf + pkt_length, m_data.data());
    m_pkt_length = pkt_length;
  }
  FECBlock(const FECHeader &h, uint16_t block_length) {
    m_data.resize(block_length + sizeof(FECHeader) - 2);
    *header() = h;
    data_length(block_length - 2);
  }

  uint16_t block_size() const {
    return (m_pkt_length + 2) - sizeof(FECHeader);
  }

  uint8_t *data() {
    return m_data.data() + sizeof(FECHeader);
  }
  const uint8_t *data() const {
    return data();
  }

  uint16_t data_length() const {
    return header()->length;
  }
  void data_length(uint16_t len) {
    header()->length = len;
    m_pkt_length = len + sizeof(FECHeader);
  }

  uint8_t *fec_data() {
    return m_data.data() + sizeof(FECHeader) - 2;
  }
  const uint8_t *fec_data() const {
    return fec_data();
  }

  FECHeader *header() {
    return reinterpret_cast<FECHeader*>(m_data.data());
  }
  const FECHeader *header() const {
    return reinterpret_cast<const FECHeader*>(m_data.data());
  }

  uint8_t *pkt_data() {
    return m_data.data();
  }
  const uint8_t *pkt_data() const {
    return pkt_data();
  }

  uint16_t pkt_length() const {
    return m_pkt_length;
  }
  void pkt_length(uint16_t len) {
    m_pkt_length = len;
  }

  bool is_data_block() const {
    return header()->block < header()->n_blocks;
  }
  bool is_fec_block() const {
    return !is_data_block();
  }

private:
  uint16_t m_pkt_length;
  std::vector<uint8_t> m_data;
};

class FECEncoder {
public:

  FECEncoder(uint8_t num_blocks = 8, uint8_t num_fec_blocks = 4, uint16_t max_block_size = 1500);

  // Get a new data block
  std::shared_ptr<FECBlock> new_block() {
    return std::shared_ptr<FECBlock>(new FECBlock(0, 0, m_num_blocks, m_num_fec_blocks, 0));
  }

  // Allocate and initialize the next data block.
  std::shared_ptr<FECBlock> get_next_block(uint16_t length = 0);

  // Add an incoming data block to be encoded
  void add_block(std::shared_ptr<FECBlock> block);

  // Retrieve the next data/fec block
  std::shared_ptr<FECBlock> get_block();
  size_t n_output_blocks() const {
    return m_out_blocks.size();
  }

private:
  uint8_t m_seq_num;
  uint8_t m_num_blocks;
  uint8_t m_num_fec_blocks;
  std::vector<std::shared_ptr<FECBlock> > m_in_blocks;
  std::queue<std::shared_ptr<FECBlock> > m_out_blocks;

  void encode_blocks();
};

struct FECDecoderStats {
  FECDecoderStats() : total_blocks(0), total_packets(0), dropped_blocks(0), dropped_packets(0),
		      lost_sync(0), bytes(0) {}
  size_t total_blocks;
  size_t total_packets;
  size_t dropped_blocks;
  size_t dropped_packets;
  size_t lost_sync;
  size_t bytes;
};

class FECDecoder {
public:

  FECDecoder();

  void add_block(const uint8_t *buf, uint16_t block_length);

  // Retrieve the next data/fec block
  std::shared_ptr<FECBlock> get_block();

  const FECDecoderStats &stats() const {
    return m_stats;
  }

private:
  // The block size of the current sequence (0 on restart)
  uint16_t m_block_size;
  // The previous header received
  FECHeader m_prev_header;
  // The blocks that have been received previously for this sequence
  std::vector<std::shared_ptr<FECBlock> > m_blocks;
  // The FEC blocks that have been received previously for this sequence
  std::vector<std::shared_ptr<FECBlock> > m_fec_blocks;
  // The output queue of blocks
  std::queue<std::shared_ptr<FECBlock> > m_out_blocks;
  // The running total of the decoder status
  FECDecoderStats m_stats;

  void decode();
};

#endif //FEC_ENCODER_HH
