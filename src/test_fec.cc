
#include <iostream>

#include <boost/program_options.hpp>

#include <logging.hh>
#include <fec.hh>

namespace po=boost::program_options;

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
  std::pair<uint32_t, double> passed = enc.test(iterations);
  LOG_INFO << passed.first << " tests passed out of " << iterations
	   << "  " << passed.second << " Mbps";
  
  return (passed.first == iterations) ? EXIT_SUCCESS : EXIT_FAILURE;
}
