/**
 * @file socket.hpp
 * @brief Declares move-only sockets and external record-length framing.
 */

#ifndef FLOW_CONTROL_SOCKET_HPP
#define FLOW_CONTROL_SOCKET_HPP

#include "record.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace flow_control
{

/** Move-only RAII ownership of one POSIX socket descriptor. */
class Socket
{
public:
  /** Creates an invalid socket. */
  Socket() noexcept;
  /** Adopts ownership of an existing descriptor. */
  explicit Socket(int descriptor) noexcept;
  ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  /** Returns whether this object owns a descriptor. */
  bool valid() const noexcept;
  /** Returns the owned descriptor, or -1. */
  int native_handle() const noexcept;
  /** Closes the descriptor; repeated calls are safe. */
  void close() noexcept;

  /** Sends every byte or throws std::system_error. */
  void send_all(const std::vector<std::uint8_t>& bytes) const;
  /** Sends a record with an external two-byte network-order length. */
  void send_record(const Record& record) const;
  /**
   * @brief Receives and validates one externally length-prefixed record.
   * @return std::nullopt only for clean EOF before any prefix byte.
   */
  std::optional<Record> receive_record() const;

  /** Accepts one connection from a listening socket. */
  Socket accept() const;

private:
  int descriptor_;
};

/** Creates a connected local socket pair for deterministic tests. */
std::pair<Socket, Socket> socket_pair();
/** Creates, binds, and listens on an IPv4 TCP port. */
Socket listen_tcp(std::uint16_t port, int backlog = 1);
/** Connects to a host and IPv4 TCP port. */
Socket connect_tcp(const std::string& host, std::uint16_t port);

}  // namespace flow_control

#endif
