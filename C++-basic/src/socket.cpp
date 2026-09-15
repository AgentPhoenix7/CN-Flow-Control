#include "socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace basic_flow {

TcpSocket::TcpSocket(int fd) : fd_(fd) {}

TcpSocket::~TcpSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

TcpSocket TcpSocket::listen_and_accept(std::uint16_t port) {
  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) throw std::runtime_error("socket() failed");

  int reuse = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("bind() failed: " + std::string(std::strerror(errno)));
  }
  if (::listen(listen_fd, 1) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("listen() failed");
  }
  int conn_fd = ::accept(listen_fd, nullptr, nullptr);
  ::close(listen_fd);
  if (conn_fd < 0) throw std::runtime_error("accept() failed");
  return TcpSocket(conn_fd);
}

TcpSocket TcpSocket::connect_to(const std::string& host, std::uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) throw std::runtime_error("socket() failed");

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    throw std::runtime_error("invalid host address: " + host);
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    throw std::runtime_error("connect() failed: " + std::string(std::strerror(errno)));
  }
  return TcpSocket(fd);
}

void TcpSocket::send_exact(const std::vector<std::uint8_t>& data) const {
  std::size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) throw std::runtime_error("send() failed");
    sent += static_cast<std::size_t>(n);
  }
}

void TcpSocket::set_recv_timeout_ms(int ms) const {
  timeval tv{};
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

RecvStatus TcpSocket::recv_timed_byte(std::uint8_t& out) const {
  ssize_t n = ::recv(fd_, &out, 1, 0);
  if (n == 1) return RecvStatus::Ok;
  if (n == 0) return RecvStatus::Closed;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return RecvStatus::Timeout;
  throw std::runtime_error("recv() failed");
}

std::vector<std::uint8_t> TcpSocket::recv_exact_blocking(std::size_t n) const {
  std::vector<std::uint8_t> buffer(n);
  std::size_t received = 0;
  while (received < n) {
    ssize_t r = ::recv(fd_, buffer.data() + received, n - received, 0);
    if (r == 0) throw std::runtime_error("connection closed mid-message");
    if (r < 0) throw std::runtime_error("recv() failed");
    received += static_cast<std::size_t>(r);
  }
  return buffer;
}

}  // namespace basic_flow

/*
SOCKET CONSTRUCTOR:

store file descriptor
*/

/*
SOCKET DESTRUCTOR:

if file descriptor is valid:
  close socket
*/

/*
MOVE SOCKET:

copy file descriptor from old object

set old object's file descriptor to -1
*/

/*
SERVER - LISTEN AND ACCEPT:

create IPv4 TCP socket

if socket creation fails:
  report error

enable address reuse

create server address:
  family = IPv4
  address = any local address
  port = requested port in network byte order

bind socket to address and port

if bind fails:
  close socket
  report error

start listening

if listen fails:
  close socket
  report error

wait for one client connection

accept connection

close listening socket

if accept fails:
  report error

return connected TCP socket
*/

/*
CLIENT - CONNECT:

create IPv4 TCP socket

if creation fails:
  report error

create server address:
  family = IPv4
  port = requested port

convert host IP string to binary IPv4 address

if address is invalid:
  close socket
  report error

connect to server

if connection fails:
  close socket
  report error

return connected TCP socket
*/

/*
SEND EXACT:

sent = 0

while sent < total data size:

  send remaining bytes

  if send fails:
    report error

  sent = sent + number of bytes sent
*/

/*
SET RECEIVE TIMEOUT:

convert milliseconds into:
  seconds
  microseconds

set socket receive timeout
*/

/*
RECEIVE TIMED BYTE:

try to receive 1 byte

if exactly 1 byte is received:
  return OK

if 0 bytes are returned:
  return Closed

if timeout occurs:
  return Timeout

otherwise:
  report receive error
*/

/*
RECEIVE EXACT:

create buffer of required size

received = 0

while received < required size:

  receive remaining bytes

  if connection closes:
    report error

  if receive fails:
    report error

  received = received + number of bytes received

return complete buffer
*/
