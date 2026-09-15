#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace basic_flow {

enum class RecvStatus { Ok, Timeout, Closed };

/// RAII wrapper around one TCP connection's file descriptor: closes it in the
/// destructor and supports safe move (no raw new/delete, no copying).
class TcpSocket {
public:
  TcpSocket() = default;
  ~TcpSocket();
  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  /// Listens on 'port' and blocks until one peer connects.
  static TcpSocket listen_and_accept(std::uint16_t port);

  /// Connects to 'host':'port'.
  static TcpSocket connect_to(const std::string& host, std::uint16_t port);

  /// Sends every byte in 'data' or throws std::runtime_error.
  void send_exact(const std::vector<std::uint8_t>& data) const;

  /// Sets the timeout applied to the next recv_timed_byte() call. 0 means block
  /// forever.
  void set_recv_timeout_ms(int ms) const;

  /// Reads exactly one byte, honoring the current timeout. Timeout/closed are
  /// returned rather than thrown, since callers treat both as protocol events.
  RecvStatus recv_timed_byte(std::uint8_t& out) const;

  /// Reads exactly 'n' more bytes, blocking indefinitely. Throws on a closed
  /// connection -- by the time this is called the peer's message has already
  /// started arriving, so an EOF here means a genuinely broken connection.
  std::vector<std::uint8_t> recv_exact_blocking(std::size_t n) const;

private:
  explicit TcpSocket(int fd);
  int fd_ = -1;
};

}  // namespace basic_flow
