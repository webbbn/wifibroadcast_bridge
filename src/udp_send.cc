
#include <wfb_bridge.hh>
#include <udp_send.hh>
#include <logging.hh>

// Retrieve messages from incoming raw socket queue and send the UDP packets.
void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, TransferStats &stats, TransferStats &stats_other) {
  double prev_time = cur_time();
  size_t write_errors = 0;
  while (1) {

    // Ralink and Atheros both always supply the FCS to userspace, no need to check
    //if (prd.m_nRadiotapFlags & IEEE80211_RADIOTAP_F_FCS)
    //bytes -= 4;

    //rx_status->adapter[adapter_no].received_packet_cnt++;
    //	rx_status->adapter[adapter_no].last_update = dbm_ts_now[adapter_no];
    //	fprintf(stderr,"lu[%d]: %lld\n",adapter_no,rx_status->adapter[adapter_no].last_update);
    //	rx_status->adapter[adapter_no].last_update = current_timestamp();

    // Pull the next block off the message queue.
    std::shared_ptr<monitor_message_t> msg = inqueue.pop();
    stats.add_rssi(msg->rssi);
    stats.add_latency(msg->latency_ms);

    // Lookup the destination class.
    if (!udp_out[msg->port]) {
      LOG_ERROR << "Error finding the output destination for port " << int(msg->port);
      continue;
    }

    // Add this block to the FEC decoder.
    std::shared_ptr<FECDecoder> fec = udp_out[msg->port]->fec();
    fec->add_block(msg->data.data(), msg->data.size());

    // Output any packets that are finished in the decoder.
    for (std::shared_ptr<FECBlock> block = fec->get_block(); block; block = fec->get_block()) {
      if (block->data_length() > 0) {
	if (udp_out[msg->port]) {
	  udp_out[msg->port]->send(send_sock, block->data(), block->data_length());
	}

	// If this is a link status message, parse it and update the stats.
	if (udp_out[msg->port]->is_status()) {
	  std::string s(block->data(), block->data() + block->data_length());
	  stats_other.update(s);
	}
      }
    }

    // Accumulate the stats
    stats.add(fec->stats(), udp_out[msg->port]->prev_stats());
    udp_out[msg->port]->prev_stats(fec->stats());
  }
}
