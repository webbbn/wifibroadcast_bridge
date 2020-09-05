
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <wifibroadcast/transfer_stats.hh>
#include <wifibroadcast/raw_socket.hh>
#include <wifibroadcast/fec.hh>
#include <wfb_bridge.hh>
#include <shared_queue.hh>

void fec_decode_thread(MessageQueue &inqueue, std::vector<std::vector<PacketQueue> > &output_queues,
                       TransferStats &stats, TransferStats &stats_other, uint8_t stats_port);

void udp_send_loop(PacketQueue &q, const std::string host, uint16_t port);

std::string hostname_to_ip(const std::string &hostname);
