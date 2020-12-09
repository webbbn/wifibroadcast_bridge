
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <wifibroadcast/transfer_stats.hh>
#include <wifibroadcast/raw_socket.hh>
#include <wifibroadcast/fec.hh>
#include <wfb_bridge.hh>
#include <shared_queue.hh>
#include <tun_interface.hh>

void fec_decode_thread(MessageQueue &inqueue, PacketQueueP output_queue,
                       TransferStats &stats);

void udp_send_loop(PacketQueueP q, const std::string host, uint16_t port);

std::string hostname_to_ip(const std::string &hostname);
