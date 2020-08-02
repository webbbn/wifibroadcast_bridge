
#pragma once

#include <string>
#include <thread>
#include <vector>

#include <INIReader.h>

#include <log_thread.hh>

int open_udp_socket_for_rx(uint16_t port, const std::string hostname, uint32_t timeout_us);

bool create_udp_to_raw_threads(SharedQueue<std::shared_ptr<Message> > &outqueue,
			       std::vector<std::shared_ptr<std::thread> > &thrs,
			       const INIReader &conf,
			       TransferStats &trans_stats,
			       TransferStats &trans_stats_other,
			       const std::string &mode,
			       const std::string &device_type);
