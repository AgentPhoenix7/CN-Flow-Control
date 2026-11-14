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

// TCP SOCKET IMPLEMENTATION
TcpSocket::TcpSocket(int fd) : fd_(fd) {}

// TCP SOCKET DESTRUCTOR
TcpSocket::~TcpSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

// TCP SOCKET MOVE CONSTRUCTOR
TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

// TCP SOCKET MOVE ASSIGNMENT OPERATOR
TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

// TCP SOCKET LISTEN AND ACCEPT
TcpSocket TcpSocket::listen_and_accept(std::uint16_t port) {
  // Create a TCP socket
  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  // Check if socket creation was successful
  if (listen_fd < 0) throw std::runtime_error("socket() failed");

  // Enable address reuse to avoid "Address already in use" errors
  int reuse = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // Set up the server address structure
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  // Bind the socket to the specified port and address
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    // If bind fails, close the socket and throw an error
    ::close(listen_fd);
    throw std::runtime_error("bind() failed: " + std::string(std::strerror(errno)));
  }
  // Start listening for incoming connections
  if (::listen(listen_fd, 1) < 0) {
    // If listen fails, close the socket and throw an error
    ::close(listen_fd);
    throw std::runtime_error("listen() failed");
  }
  // Accept a single incoming connection
  int conn_fd = ::accept(listen_fd, nullptr, nullptr);
  ::close(listen_fd);
  // If accept fails, throw an error
  if (conn_fd < 0) throw std::runtime_error("accept() failed");
  // Return a TcpSocket object representing the accepted connection
  return TcpSocket(conn_fd);
}

// TCP SOCKET CONNECT TO
TcpSocket TcpSocket::connect_to(const std::string& host, std::uint16_t port) {
  // Create a TCP socket
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  // Check if socket creation was successful
  if (fd < 0) throw std::runtime_error("socket() failed");

  // Set up the server address structure
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // Convert the host string to a binary IPv4 address
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    // If the host address is invalid, close the socket and throw an error
    ::close(fd);
    throw std::runtime_error("invalid host address: " + host);
  }
  // Attempt to connect to the server
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    // If the connection fails, close the socket and throw an error
    ::close(fd);
    throw std::runtime_error("connect() failed: " + std::string(std::strerror(errno)));
  }
  // Return a TcpSocket object representing the connected socket
  return TcpSocket(fd);
}

// TCP SOCKET SEND EXACT
void TcpSocket::send_exact(const std::vector<std::uint8_t>& data) const {
  // Keep track of the number of bytes sent
  std::size_t sent = 0;
  // Loop until all bytes in 'data' have been sent
  while (sent < data.size()) {
    // Attempt to send the remaining bytes
    ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
    // If send fails, throw an error
    if (n <= 0) throw std::runtime_error("send() failed");
    // Update the number of bytes sent
    sent += static_cast<std::size_t>(n);
  }
}

// TCP SOCKET SET RECEIVE TIMEOUT
void TcpSocket::set_recv_timeout_ms(int ms) const {
  // Convert milliseconds to seconds and microseconds
  timeval tv{};
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  // Set the socket receive timeout option
  ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// TCP SOCKET RECEIVE TIMED BYTE
RecvStatus TcpSocket::recv_timed_byte(std::uint8_t& out) const {
  // Attempt to receive a single byte from the socket
  ssize_t n = ::recv(fd_, &out, 1, 0);
  // Check the result of the receive operation
  if (n == 1) return RecvStatus::Ok;
  if (n == 0) return RecvStatus::Closed;
  // If the receive operation failed, check if it was due to a timeout
  if (errno == EAGAIN || errno == EWOULDBLOCK) return RecvStatus::Timeout;
  // If the receive operation failed for another reason, throw an error
  throw std::runtime_error("recv() failed");
}

// TCP SOCKET RECEIVE EXACT BLOCKING
std::vector<std::uint8_t> TcpSocket::recv_exact_blocking(std::size_t n) const {
  // Create a buffer to hold the received bytes
  std::vector<std::uint8_t> buffer(n);
  // Keep track of the number of bytes received
  std::size_t received = 0;
  // Loop until the required number of bytes have been received
  while (received < n) {
    // Attempt to receive the remaining bytes
    ssize_t r = ::recv(fd_, buffer.data() + received, n - received, 0);
    // If the connection is closed mid-message, throw an error
    if (r == 0) throw std::runtime_error("connection closed mid-message");
    if (r < 0) throw std::runtime_error("recv() failed");
    // Update the number of bytes received
    received += static_cast<std::size_t>(r);
  }
  // Return the complete buffer containing the received bytes
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
