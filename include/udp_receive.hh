
#pragma once

#include <string>

int open_udp_socket_for_rx(uint16_t port, const std::string hostname, uint32_t timeout_us);
