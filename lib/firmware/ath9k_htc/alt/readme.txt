apply the patch to this repo: https://github.com/qca/open-ath9k-htc-firmware.git

the provided firmware binary is configured to use MCS3 (26mbit/s) as the injection rate

2019-12-26 Brian Webb
---------------------

- Evaluated these firmwares as direct replacements to the standard firmware to try to increase the throughput, which appears to be capped at ~8-10Mbps. Inject failures on the Tx side are observed beyond that.
  - Simply renamed each firmware to htc_9271-1.4.0.fw without changing any other parameters
  - htc_9271.fw: No improvement
  - htc_9271.fw.mcs1: No inject failures on the Tx side, but a similar amount of dropouts on the Rx side.
  - htc_9271.fw.mcs3: No inject failures on the Tx side, but a similar amount of dropouts on the Rx side.
  - htc_9271.fw.24mbitofdm: No inject failures on the Tx side. Dropouts on Rx side a little high, but much better.
