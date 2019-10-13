
#include <math.h>

#include <fec.hh>

FECEncoder::FECEncoder(uint8_t num_blocks, uint8_t num_fec_blocks, uint16_t max_block_size) :
  m_num_blocks(num_blocks), m_num_fec_blocks(num_fec_blocks), m_seq_num(0) {
  // Ensure that the FEC library is initialized
  // This may not work with multiple threads!
  fec_init();
}

// Allocate and initialize the next data block.
std::shared_ptr<FECBlock> FECEncoder::get_next_block(uint16_t length) {
  return std::shared_ptr<FECBlock>(new FECBlock(m_seq_num, m_in_blocks.size(), m_num_blocks,
						m_num_fec_blocks, length));
}

// Add an incoming data block to be encoded
void FECEncoder::add_block(std::shared_ptr<FECBlock> block) {
  FECHeader *h = block->header();
  h->block = m_in_blocks.size();
  m_in_blocks.push_back(block);

  // Calculate the FEC blocks when we've received enough blocks.
  if ((m_num_fec_blocks > 0) && (h->block == (m_num_blocks - 1))) {
    encode_blocks();
  }
}

// Retrieve the next data/fec block
std::shared_ptr<FECBlock> FECEncoder::get_block() {
  if (m_out_blocks.empty()) {
    return std::shared_ptr<FECBlock>();
  }
  std::shared_ptr<FECBlock> ret = m_out_blocks.front();
  m_out_blocks.pop();
  return ret;
}

// Complete the sequence with the current set of blocks
void FECEncoder::flush() {
  encode_blocks();
}

void FECEncoder::encode_blocks() {
  uint8_t num_blocks = m_in_blocks.size();
  if (num_blocks == 0) {
    return;
  }

  // Create the FEC arrays of pointers to the data blocks.
  std::vector<uint8_t*> data_blocks(m_num_blocks);
  uint16_t block_size = 0;
  for (uint8_t i = 0; i < num_blocks; ++i) {
    std::shared_ptr<FECBlock> block = m_in_blocks[i];
    data_blocks[i] = block->fec_data();
    block_size = std::max(block_size, static_cast<uint16_t>(block->header()->length + 2));
    block->header()->n_blocks = num_blocks;
    m_out_blocks.push(block);
  }

  // Create the output FEC blocks
  std::vector<uint8_t*> fec_blocks(m_num_fec_blocks);
  for (uint8_t i = 0; i < m_num_fec_blocks; ++i) {
    std::shared_ptr<FECBlock> block(new FECBlock(m_seq_num, num_blocks + i, num_blocks,
						 m_num_fec_blocks, block_size - 2));
    fec_blocks[i] = block->fec_data();
    block->pkt_length(block_size + sizeof(FECHeader) - 2);
    m_out_blocks.push(block);
  }

  // Encode the blocks.
  fec_encode(block_size, data_blocks.data(), num_blocks, fec_blocks.data(), m_num_fec_blocks);

  // Prepare for the next set of blocks.
  ++m_seq_num;
  m_in_blocks.clear();
}

FECDecoder::FECDecoder() : m_block_size(0) {
  fec_init();
}

void FECDecoder::add_block(const uint8_t *buf, uint16_t block_length) {
  std::shared_ptr<FECBlock> blk(new FECBlock(buf, block_length));
  const FECHeader &h = *blk->header();
  uint8_t n_blocks = h.n_blocks;
  uint8_t n_fec_blocks = h.n_fec_blocks;
  FECHeader &ph = m_prev_header;
  uint16_t unrolled_prev_seq = static_cast<uint16_t>(ph.seq_num);
  uint16_t unrolled_seq = static_cast<uint16_t>(h.seq_num);
  if (unrolled_prev_seq > unrolled_seq) {
    unrolled_seq += 256;
  }
  ++m_stats.total_packets;
  m_stats.bytes += block_length;

  // Did we reach the end of a sequence without getting enough blocks?
  if ((m_block_size != 0) && (unrolled_prev_seq != unrolled_seq)) {

    // If se get a (unrolled) sequence number that is less than the previous sequence number
    // we've obviously lost sync.
    if (unrolled_seq < unrolled_prev_seq) {
      ++m_stats.lost_sync;
    } else {

      // Calculate how many packets we dropped with this break in the sequence.
      m_stats.dropped_blocks += unrolled_seq - unrolled_prev_seq;

      // Calculate how many packets we dropped.
      uint64_t pbn = unrolled_prev_seq * static_cast<uint64_t>(n_blocks + n_fec_blocks) + ph.block;
      uint64_t bn = unrolled_seq * static_cast<uint64_t>(n_blocks + n_fec_blocks) + h.block;
      if (pbn < bn) {
	m_stats.dropped_packets += bn - pbn;
      }
    }

    // Reset the sequence.
    m_block_size = 0;
    m_blocks.clear();
    m_fec_blocks.clear();

  } else if(h.block <= ph.block) {

    // This shouldn't happen.
    ++m_stats.dropped_packets;
  } else {

    // Record any dropped packets from the last packet.
    m_stats.dropped_packets += (h.block - ph.block) - 1;
  }
  ph = h;

  // Have we completed the current block?
  if ((m_block_size == 0) && (unrolled_prev_seq == unrolled_seq)) {
    return;
  }

  // The current block size is equal to the block size of the largest block.
  m_block_size = std::max(m_block_size, blk->block_size());

  // Is this a data block or FEC block?
  if (blk->is_data_block()) {

    // Add this block to the list of blocks.
    m_blocks.push_back(blk);

    // Release the block if we don't have a gap.
    if ((m_blocks.size() - 1) == h.block) {
      m_out_blocks.push(blk);
    }

    // Have we reached the end of the data blocks without dropping a packet?
    if (m_blocks.size() == n_blocks) {
      m_block_size = 0;
      m_blocks.clear();
      m_fec_blocks.clear();
      ++m_stats.total_blocks;
    }

  } else {

    // Add this block to the list of FEC blocks.
    m_fec_blocks.push_back(blk);

    // Decode once we've received anough blocks + FEC blocks and have dropped a block.
    if ((m_blocks.size() + m_fec_blocks.size()) == n_blocks) {

      // Decode the sequence
      decode();

      // Start waiting for blocks from the next packet.
      m_block_size = 0;
      m_blocks.clear();
      m_fec_blocks.clear();
      ++m_stats.total_blocks;
    }
  }
}

void FECDecoder::decode() {
  const FECHeader &h = *m_blocks[0]->header();
  uint8_t n_blocks = h.n_blocks;
  uint8_t n_fec_blocks = h.n_fec_blocks;

  // Create the vector of data blocks.
  std::vector<uint8_t*> block_ptrs(h.n_blocks, 0);
  std::vector<std::shared_ptr<FECBlock> > blocks(h.n_blocks);
  for (auto block : m_blocks) {
    blocks[block->header()->block] = block;
    block_ptrs[block->header()->block] = block->fec_data();
  }

  // Create the erased blocks array
  std::vector<std::shared_ptr<FECBlock> > fec_blocks;
  std::vector<unsigned int> erased_block_idxs;
  for (size_t i = 0; i < h.n_blocks; ++i) {
    if (!block_ptrs[i]) {
      std::shared_ptr<FECBlock> blk(new FECBlock(h, m_block_size));
      erased_block_idxs.push_back(i);
      fec_blocks.push_back(blk);
      blocks[i] = blk;
      block_ptrs[i] = blk->fec_data();
    }
  }

  // Create the FEC blocks array
  std::vector<uint8_t*> fec_block_ptrs;
  std::vector<unsigned int> fec_block_idxs;
  for (auto block : m_fec_blocks) {
    uint8_t fec_block_idx = block->header()->block - block->header()->n_blocks;
    fec_block_ptrs.push_back(block->fec_data());
    fec_block_idxs.push_back(fec_block_idx);
  }

  // Decode the blocks
  fec_decode(m_block_size,
	     block_ptrs.data(),
	     n_blocks,
	     fec_block_ptrs.data(),
	     fec_block_idxs.data(),
	     erased_block_idxs.data(),
	     erased_block_idxs.size());

  // Send the remainder of blocks that have a reasonable length.
  for (size_t i = erased_block_idxs[0]; i < n_blocks; ++i) {
    uint16_t length = blocks[i]->data_length();
    if (length <= m_block_size) {
      m_out_blocks.push(blocks[i]);
    } else {
      ++m_stats.dropped_blocks;
    }
  }
}

// Retrieve the next data/fec block
std::shared_ptr<FECBlock> FECDecoder::get_block() {
  if (m_out_blocks.empty()) {
    return std::shared_ptr<FECBlock>();
  }
  std::shared_ptr<FECBlock> ret = m_out_blocks.front();
  m_out_blocks.pop();
  return ret;
}

FECDecoderStats operator-(const FECDecoderStats& s1, const FECDecoderStats &s2) {
  FECDecoderStats ret;
  ret.total_blocks = s1.total_blocks - s2.total_blocks;
  ret.total_packets = s1.total_packets - s2.total_packets;
  ret.dropped_blocks = s1.dropped_blocks - s2.dropped_blocks;
  ret.dropped_packets = s1.dropped_packets - s2.dropped_packets;
  ret.lost_sync = s1.lost_sync - s2.lost_sync;
  ret.bytes = s1.bytes - s2.bytes;
  return ret;
}

FECDecoderStats operator+(const FECDecoderStats& s1, const FECDecoderStats &s2) {
  FECDecoderStats ret;
  ret.total_blocks = s1.total_blocks + s2.total_blocks;
  ret.total_packets = s1.total_packets + s2.total_packets;
  ret.dropped_blocks = s1.dropped_blocks + s2.dropped_blocks;
  ret.dropped_packets = s1.dropped_packets + s2.dropped_packets;
  ret.lost_sync = s1.lost_sync + s2.lost_sync;
  ret.bytes = s1.bytes + s2.bytes;
  return ret;
}
