#pragma once

#include <string>
#include <vector>

class TUNInterface {
public:

  TUNInterface(const std::string &dev_name = "wfbtun", const std::string &tundev = "/dev/net/tun");

  bool init(const std::string &ip_addr, const std::string &subnet_mask, uint16_t mtu);

  bool read(std::vector<uint8_t> &buf, uint32_t timeout_us = 0);
  bool write(const uint8_t *data, size_t size);

private:
  std::string m_dev_name;
  std::string m_tun_dev;
  int m_fd;
};
