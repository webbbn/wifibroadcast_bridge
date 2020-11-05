
#include <sys/time.h>

#include <iostream>

#include <cmdparser.hpp>

#include <wifibroadcast/fec.hh>
#include <logging.hh>

inline double cur_time() {
  struct timeval t;
  gettimeofday(&t, 0);
  return double(t.tv_sec) + double(t.tv_usec) * 1e-6;
}

std::pair<uint32_t, double> run_test(FECBufferEncoder &enc, uint32_t max_block_size,
				     uint32_t iterations) {
  FECDecoder dec;
  uint32_t min_buffer_size = max_block_size * 6;
  uint32_t max_buffer_size = max_block_size * 100;

  uint32_t failed = 0;
  size_t bytes = 0;
  double start_time = cur_time();
  for (uint32_t i = 0; i < iterations; ++i) {

    // Create a random buffer of data
    uint32_t buf_size = min_buffer_size + rand() % (max_buffer_size - min_buffer_size);
    bytes += buf_size;
    std::vector<uint8_t> buf(buf_size);
    for (uint32_t j = 0; j < buf_size; ++j) {
      buf[j] = rand() % 255;
    }
    std::cout << "Iteration: " << i << "  buffer size: " << buf_size << std::endl;

    // Encode it
    std::vector<std::shared_ptr<FECBlock> > blks = enc.encode_buffer(buf);
    std::cerr << blks.size() << " blocks created\n";

    // Decode it
    std::vector<uint8_t> obuf;
    uint32_t drop_count = 0;
    for (std::shared_ptr<FECBlock> blk : blks) {
      if ((rand() % 10) != 0) {
	dec.add_block(blk->pkt_data(), blk->pkt_length());
      }
    }
    for (std::shared_ptr<FECBlock> sblk = dec.get_block(); sblk; sblk = dec.get_block()) {
      std::copy(sblk->data(), sblk->data() + sblk->data_length(),
		std::back_inserter(obuf));
    }

    // Compare
    if (obuf.size() != buf.size()) {
      std::cerr << "Buffers are different sizes: " << obuf.size() << " != " << buf.size()
                << std::endl;
      ++failed;
    } else {
      for (size_t j = 0; j < buf.size(); ++j) {
	if (obuf[j] != buf[j]) {
	  std::cerr << "Buffers differ at location " << j << ": " << obuf[j] << " != " << buf[j]
                    << std::endl;
	  ++failed;
	  break;
	}
      }
    }
  }

  return std::make_pair(iterations - failed,
			8e-6 * static_cast<double>(bytes) / (cur_time() - start_time));
}


int main(int argc, char** argv) {

  cli::Parser options(argc, argv);
  options.set_optional<uint16_t>
    ("i", "iterations", 1000, "the number of iterations to run");
  options.set_optional<uint16_t>
    ("b", "block_size", 1400, "the block size to test");
  options.set_optional<float>
    ("f", "fec_ratio", 0.5, "the FEC block / data block ratio");

  options.run_and_exit_if_error();
  uint32_t iterations = options.get<uint32_t>("i");
  uint32_t block_size = options.get<uint32_t>("b");
  float fec_ratio = options.get<float>("f");

  // Configure logging
  log4cpp::Appender *appender1 = new log4cpp::OstreamAppender("console", &std::cout);
  appender1->setLayout(new log4cpp::BasicLayout());
  log4cpp::Category& root = log4cpp::Category::getRoot();
  root.setPriority(log4cpp::Priority::DEBUG);
  root.addAppender(appender1);
  
  FECBufferEncoder enc(block_size, fec_ratio);
  std::pair<uint32_t, double> passed = run_test(enc, block_size, iterations);
  LOG_INFO << passed.first << " tests passed out of " << iterations
           << "  " << passed.second << " Mbps";
  
  return (passed.first == iterations) ? EXIT_SUCCESS : EXIT_FAILURE;
}
