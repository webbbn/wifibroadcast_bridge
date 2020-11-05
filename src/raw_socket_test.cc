
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <iostream>

#include <cmdparser.hpp>

#include <wifibroadcast/fec.hh>
#include <wifibroadcast/raw_socket.hh>
#include <logging.hh>

#define MAX_MESSAGE 65535

class USecTimer {
public:
  USecTimer() {
    struct timeval t;
    gettimeofday(&t, 0);
    m_start_second = t.tv_sec;
  }

  uint64_t usec() const {
    struct timeval t;
    gettimeofday(&t, 0);
    return static_cast<uint64_t>(t.tv_sec - m_start_second) * 1000000 +
      static_cast<uint64_t>(t.tv_usec);
  }

private:
  time_t m_start_second;
};


int main(int argc, char** argv) {

  // Ensure we've running with root privileges
  auto uid = getuid();
  if (uid != 0) {
    std::cerr << "This application must be run as root or with root privileges.\n";
    return EXIT_FAILURE;
  }

  cli::Parser options(argc, argv);
  options.set_optional<bool>
    ("v", "verbose", false, "output debug messages");
  options.set_optional<uint16_t>
    ("p", "port", 0, "the port number (0-16) to send to");
  options.set_optional<uint16_t>
    ("d", "datarate", 3, "the MCS datarate");
  options.set_optional<uint16_t>
    ("l", "length", 1024, "the length of message to send");
  options.set_optional<uint16_t>
    ("D", "data_blocks", 0, "the number of data blocks per FEC sequence");
  options.set_optional<uint16_t>
    ("F", "fec_blocks", 0, "the number of fec blocks per FEC sequence");
  options.set_optional<bool>
    ("r", "receiver", false, "receive messages instead of sending them");
  options.set_optional<std::string>
    ("m", "message", "Hello World!", "the test message to send");
  options.set_optional<uint64_t>
    ("P", "period", 0, "the time between messages in microseconds");
  options.set_optional<float>
    ("f", "frequency", 0, "change the frequency of the wifi channel");
  options.set_required<std::string>
    ("c", "conf_file", "the path to the configuration file used for configuring ports");
  options.set_required<std::string>
    ("D", "device", "the wifi device to connect to");
  options.run_and_exit_if_error();

  bool do_configure = !options.get<bool>("n");
  std::string conf_file = options.get<std::string>("c");

  uint16_t port = options.get<uint16_t>("p");
  uint64_t period = options.get<uint64_t>("P");
  uint16_t length = options.get<uint16_t>("l");
  float frequency = options.get<float>("f");
  uint16_t datarate = options.get<uint16_t>("d");
  uint16_t num_data_blocks = options.get<uint16_t>("D");
  uint16_t num_fec_blocks = options.get<uint16_t>("F");
  std::string message = options.get<std::string>("m");
  std::string device = options.get<std::string>("D");
  bool receiver = options.get<bool>("r");
  bool verbose = options.get<bool>("v");

  // Configure logging
  log4cpp::Appender *appender1 = new log4cpp::OstreamAppender("console", &std::cout);
  appender1->setLayout(new log4cpp::BasicLayout());
  log4cpp::Category& root = log4cpp::Category::getRoot();
  root.setPriority(verbose ? log4cpp::Priority::DEBUG : log4cpp::Priority::INFO);
  root.addAppender(appender1);

  // Make sure the adapter is in monitor mode.
  if (!set_wifi_monitor_mode(device)) {
    LOG_ERROR << "Error configuring the device in monitor mode";
    return EXIT_FAILURE;
  }

  // Set the frequency if the user requested it.
  if (frequency != 0) {
    if (!set_wifi_frequency(device, frequency)) {
      LOG_ERROR << "Error changing the frequency to: " << frequency;
      return EXIT_FAILURE;
    } else {
      LOG_DEBUG << "Frequency changed to: " << frequency;
    }
  }

  // Allocate a message buffer
  uint8_t msgbuf[MAX_MESSAGE];
  USecTimer time;

  if (receiver) {
    LOG_DEBUG << "Receiving test messages on port " << static_cast<int>(port);

    // Create the FEC decoder
    FECDecoder dec;

    // Try to open the device
    RawReceiveSocket raw_recv_sock(true);
    if (raw_recv_sock.add_device(device)) {
      LOG_DEBUG << "Receiving on interface: " << device;
    } else {
      LOG_DEBUG << "Error opening the raw socket for transmiting: " << device;
      return EXIT_FAILURE;
    }

    monitor_message_t msg;
    uint16_t prev_pktid = 0;
    uint64_t bytes = 0;
    uint32_t blocks = 0;
    uint32_t packets = 0;
    uint32_t dropped_blocks = 0;
    uint32_t dropped_packets = 0;
    uint64_t prev_bytes = 0;
    uint32_t prev_blocks = 0;
    uint32_t prev_packets = 0;
    uint32_t prev_dropped_blocks = 0;
    uint32_t prev_dropped_packets = 0;
    uint64_t last_status = time.usec();
    while (raw_recv_sock.receive(msg, std::chrono::milliseconds(200))) {
      size_t len = msg.data.size();
      if (len > 0) {
        dec.add_block(msg.data.data(), msg.data.size());
      }
      for (std::shared_ptr<FECBlock> block = dec.get_block(); block;
           block = dec.get_block()) {
      }
      uint64_t t = time.usec();
      if ((t - last_status) > 1000000) {
        bytes = dec.stats().bytes;
        blocks = dec.stats().total_blocks;
        packets = dec.stats().total_packets;
        dropped_packets = dec.stats().dropped_packets;
        dropped_blocks = dec.stats().dropped_blocks;
        float mbps = static_cast<float>(bytes - prev_bytes) * 8.0 / 1e6;
        LOG_INFO << mbps << " Mbps  Packets (eb/ep/tot): "
                 << (dropped_blocks - prev_dropped_blocks) << "/"
                 << (dropped_packets - prev_dropped_packets) << "/"
                 << (packets - prev_packets);
        prev_bytes = bytes;
        prev_dropped_blocks = dropped_blocks;
        prev_dropped_packets = dropped_packets;
        prev_packets = packets;
        last_status = t;
      }
    }

  } else {
    LOG_DEBUG << "Sending test messages to port " << static_cast<int>(port);

    // Create the FEC encoder
    FECEncoder enc(num_data_blocks, num_fec_blocks, length);

    // Try to open the device
    RawSendSocket raw_send_sock(false);
    if (raw_send_sock.add_device(device, true, true, true, true)) {
      LOG_DEBUG << "Transmitting on interface: " << device;
    } else {
      LOG_DEBUG << "Error opening the raw socket for transmiting: " << device;
      return EXIT_FAILURE;
    }

    uint64_t send_time = 0;
    while (1) {
      uint64_t t = time.usec();
      if ((t - send_time) >= period) {
        {
          std::shared_ptr<FECBlock> block = enc.get_next_block(length);
          if (!message.empty()) {
            memcpy(block->data(), reinterpret_cast<const uint8_t*>(message.c_str()), message.size());
            length = message.size();
          } else if (length > 0) {
            memset(block->data(), 0, length);
          } else {
            continue;
          }
          enc.add_block(block);
        }
        for (std::shared_ptr<FECBlock> block = enc.get_block(); block;
             block = enc.get_block()) {
          raw_send_sock.send(block->pkt_data(), block->pkt_length(), port, DATA_LINK,
                             datarate);
        }
        if (period == 0) {
          break;
        }
        send_time = t;
      }
    }
  }
}
