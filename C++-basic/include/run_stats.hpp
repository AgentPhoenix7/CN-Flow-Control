#pragma once

namespace basic_flow {

/// The handful of numbers the assignment asks the three protocols to be compared
/// on: completion time, and enough raw counters to compute RTT/efficiency from.
struct RunStats {
  int frames_sent = 0;         ///< every DATA transmission, including retransmissions
  int retransmissions = 0;     ///< subset of frames_sent that were retransmissions
  int acks_received = 0;       ///< valid, matching ACKs received
  int timeouts = 0;            ///< rounds where no ACK arrived before the timeout
  double elapsed_ms = 0.0;     ///< wall-clock time for the whole transfer
};

}  // namespace basic_flow
