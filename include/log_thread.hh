
#pragma once

#include <memory>

#include <udp_send.hh>
#include <wifibroadcast/transfer_stats.hh>

void log_thread(TransferStats &stats, TransferStats &stats_other, float syslog_period,
		float status_period, PacketQueueP log_out, PacketQueueP packed_log_out,
                PacketQueueP log_in, const uint16_t port_lut[]);
