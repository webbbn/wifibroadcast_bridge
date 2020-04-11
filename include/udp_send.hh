
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <wifibroadcast/transfer_stats.hh>
#include <wifibroadcast/raw_socket.hh>
#include <wifibroadcast/fec.hh>
#include <udp_destination.hh>
#include <wfb_bridge.hh>
#include <shared_queue.hh>

std::string hostname_to_ip(const std::string &hostname);

void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, TransferStats &stats, TransferStats &stats_other);
