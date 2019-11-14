
#include <iostream>

#include <boost/program_options.hpp>

#include <logging.hh>
#include <fec.hh>

namespace po=boost::program_options;


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
    LOG_DEBUG << "Iteration: " << i << "  buffer size: " << buf_size;

    // Encode it
    std::vector<std::shared_ptr<FECBlock> > blks = enc.encode_buffer(buf);
    LOG_DEBUG << blks.size() << " blocks created";

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
      LOG_ERROR << "Buffers are different sizes: " << obuf.size() << " != " << buf.size();
      ++failed;
    } else {
      for (size_t j = 0; j < buf.size(); ++j) {
	if (obuf[j] != buf[j]) {
	  LOG_ERROR << "Buffers differ at location " << j << ": " << obuf[j] << " != " << buf[j];
	  ++failed;
	  break;
	}
      }
    }
  }

  return std::make_pair(iterations - failed,
			8e-6 * static_cast<double>(bytes) / (cur_time() - start_time));
}


int main(int argc, const char** argv) {
  uint32_t iterations;
  uint32_t block_size;
  float fec_ratio;
  std::string log_level;

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "produce help message")
    ("iterations", po::value<uint32_t>(&iterations)->default_value(1000),
     "the number of iterations to run")
    ("block_size", po::value<uint32_t>(&block_size)->default_value(1400),
     "the block size to test")
    ("fec_ratio", po::value<float>(&fec_ratio)->default_value(0.5),
     "the FEC block / data block ratio")
    ("log_level", po::value<std::string>(&log_level)->default_value("info"),
     "the log level for priting debug/info")
    ;

  po::options_description all_options("Allowed options");
  all_options.add(desc);
  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(all_options).run(), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << "Usage: " << argv[0] << " [options]\n";
    std::cout << desc << std::endl;
    return EXIT_SUCCESS;
  }

  // Create the logger
  Logger::create(log_level);

  FECBufferEncoder enc(block_size, fec_ratio);
  std::pair<uint32_t, double> passed = run_test(enc, block_size, iterations);
  LOG_INFO << passed.first << " tests passed out of " << iterations
	   << "  " << passed.second << " Mbps";
  
  return (passed.first == iterations) ? EXIT_SUCCESS : EXIT_FAILURE;
}
