
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

void fec_decode_thread(MessageQueue &inqueue, PacketQueues *output_queues,
                       TransferStats &stats, TransferStats &stats_other, uint8_t stats_port);

void udp_send_loop(PacketQueueP q, int send_sock, const std::string host, uint16_t port);
void tun_send_loop(PacketQueueP q, std::shared_ptr<TUNInterface> tun_interface);

std::string hostname_to_ip(const std::string &hostname);
