
#pragma once

#include <string>
#include <thread>
#include <vector>

#include <INIReader.h>

#include <log_thread.hh>
#include <tun_interface.hh>

int open_udp_socket_for_rx(uint16_t port, const std::string hostname, uint32_t timeout_us);

void udp_recv_loop(PacketQueueP outq, const std::string hostname, uint16_t port,
                   uint32_t blocksize);

void tun_raw_thread(TUNInterface &tun_interface,
                    SharedQueue<std::shared_ptr<Message> > &outqueue,
                    std::map<uint16_t, std::shared_ptr<Message> > &port_lut,
                    uint32_t timeout_us);
