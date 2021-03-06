
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>

#include <tun_interface.hh>
#include <logging.hh>
#include <wfb_bridge.hh>

#define PROTOCOL_UDP 0x11
#define PROTOCOL_TCP 0x06

TUNInterface::TUNInterface(const std::string &dev_name, const std::string &tundev)
  : m_dev_name(dev_name), m_tun_dev(tundev), m_fd(0) {
}

bool TUNInterface::init(const std::string &ip_addr, const std::string &subnet_mask, uint16_t mtu) {

  // Create the TUN interface
  if ((m_fd = open(m_tun_dev.c_str(), O_RDWR)) < 0) {
    LOG_ERROR << "Error opening the tun device: " << m_tun_dev;
    return false;
  }

  // Configure the TUN interface
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  strncpy(ifr.ifr_name, m_dev_name.c_str(), IFNAMSIZ);
  if(ioctl(m_fd, TUNSETIFF, (void *)&ifr) < 0) {
    LOG_ERROR << "Error in ioctl(TUNSETIFF)";
    close(m_fd);
    return false;
  }

  // Create the socket for configuring the TUN interface
  int tun_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (tun_sock < 0) {
    LOG_ERROR << "Error creating the TUN socket";
    close(m_fd);
    return false;
  }

  // Configure the IP address of the TUN interface
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, m_dev_name.c_str(), IFNAMSIZ);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  if (inet_pton(AF_INET, ip_addr.c_str(), &(addr.sin_addr)) != 1) {
    LOG_ERROR << "Invalid IP address: " << ip_addr << strerror(errno);
    return false;
  }
  memcpy(&(ifr.ifr_addr), &addr, sizeof(struct sockaddr));
  if (ioctl(tun_sock, SIOCSIFADDR, &ifr) < 0) {
    LOG_ERROR << "Error setting the IP address: " << ip_addr << "  "
              << strerror(errno);
    return false;
  }

  // Configure the subnet mask of the TUN interface
  struct sockaddr_in subnet_mask_in;
  memset(&subnet_mask_in, 0, sizeof(subnet_mask_in));
  subnet_mask_in.sin_family = AF_INET;
  if (inet_pton(AF_INET, subnet_mask.c_str(), &(subnet_mask_in.sin_addr)) != 1) {
    LOG_ERROR << "Invalid subnet mask: " << subnet_mask << strerror(errno);
    return false;
  }
  memcpy(&(ifr.ifr_addr), &subnet_mask_in, sizeof (struct sockaddr));
  if(ioctl(tun_sock, SIOCSIFNETMASK, &ifr) < 0) {
    LOG_ERROR << "Error setting the subnet mask: " << subnet_mask << "  "
              << strerror(errno);
    return false;
  }

  // Get the current flags from the TUN device
  if (ioctl(tun_sock, SIOCGIFFLAGS, &ifr) < 0) {
    LOG_ERROR << "Error getting the TUN device flags: " << strerror(errno);
    close(m_fd);
    close(tun_sock);
    return false;
  }

  // Bring the interface up
  ifr.ifr_flags |= IFF_UP;
  ifr.ifr_flags |= IFF_RUNNING;
  if (ioctl(tun_sock, SIOCSIFFLAGS, &ifr) < 0)  {
    LOG_ERROR << "Error bringing up the TUN device: " << strerror(errno);
    close(m_fd);
    close(tun_sock);
    return false;
  }

  // Configure the MTU of the TUN interface
  ifr.ifr_mtu = mtu;
  if (ioctl(tun_sock, SIOCSIFMTU, &ifr) < 0)  {
    LOG_ERROR << "Error setting the MTU of the TUN interface to " << mtu << " ("
              << strerror(errno) << ")";
    close(m_fd);
    close(tun_sock);
    return false;
  }

  // Close the NET kernel interface
  close(tun_sock);

  // Success!
  return true;
}

bool TUNInterface::read(std::vector<uint8_t> &buf, uint16_t &ip_port, uint32_t timeout_us) {
  if (buf.empty()) {
    return false;
  }

  if (timeout_us > 0) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(m_fd, &set);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = timeout_us;
    int ret = select(m_fd + 1, &set, NULL, NULL, &timeout);
    if (ret == -1) {
      return false;
    } else if (ret == 0) {
      buf.clear();
      return true;
    }
  }

  // Read the data that's available
  int nread = ::read(m_fd, buf.data(), buf.size());
  if (nread < 0) {
    buf.clear();
    return false;
  }
  buf.resize(nread);

  // Extract the protocol from the IP header.
  uint8_t protocol;
  if (nread > 9) {
    protocol = buf[9];
  }

  // Parse IP port out of the header.
  if (((protocol == PROTOCOL_UDP) || (protocol == PROTOCOL_TCP)) && (nread > 23)) {
    ip_port = (static_cast<uint16_t>(buf[22]) << 8) | static_cast<uint16_t>(buf[23]);
  } else {
    ip_port = 0;
  }

  return true;
}

bool TUNInterface::write(const uint8_t *data, size_t size) {
  if (data && size && (::write(m_fd, data, size) == size)) {
    return true;
  }
  return false;
}

void tun_raw_thread(TUNInterface &tun_interface,
                    SharedQueue<std::shared_ptr<Message> > &outqueue,
                    std::map<uint16_t, std::shared_ptr<Message> > &port_lut,
                    uint32_t timeout_us) {
  double last_send_time = 0;

  while (1) {

    // Receive the next message.
    // 1ms timeout for FEC links to support flushing
    // Update rate target every 100 uS
    //uint32_t timeout_us = (rate_target > 0) ? 100 : (do_fec ? 1000 : 0);
    std::vector<uint8_t> msg(2048);
    uint16_t ip_port;
    tun_interface.read(msg, ip_port, timeout_us);

    // Did we receive a message to send?
    if (!msg.empty()) {

      // Lookup the IP port in the LUT.
      std::map<uint16_t, std::shared_ptr<Message> >::const_iterator itr = port_lut.find(ip_port);
      std::shared_ptr<Message> proto;
      if (itr != port_lut.end()) {
        proto = itr->second;
      } else if ((itr = port_lut.find(0)) != port_lut.end()) {
        proto = itr->second;
      } else {
        LOG_WARNING << "Did not find WFB port for IP port " << ip_port << " or default TUN port 0";
        continue;
      }

      // Add the mesage to the output queue
      outqueue.push(proto->copy(msg, ip_port));

    } else {
      // flush all the encoders that are not flushed
      for (auto itr : port_lut) {
        auto proto = itr.second;
        if (!proto->enc->is_flushed()) {
          outqueue.push(proto->copy(std::vector<uint8_t>(), proto->ip_port));
        }
      }
    }
  }
}
