
#pragma once

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <vector>
#include <string>
#include <memory>
#include <thread>

#include <log_thread.hh>

int open_udp_socket_for_rx(uint16_t port, const std::string hostname, uint32_t timeout_us);
bool create_udp_receive_threads(boost::property_tree::ptree &conf, const std::string &mode,
				uint16_t status_port, float syslog_period, float status_period,
				std::string &status_host,
				SharedQueue<std::shared_ptr<Message> > &outqueue,
				TransferStats &trans_stats, TransferStats &trans_stats_other,
				std::vector<std::shared_ptr<std::thread> > &thrs);
