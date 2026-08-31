#include "socket.hpp"

#include "config.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace flow_control
{

namespace
{

enum class ReceiveExactResult
{
  Complete,
  CleanEof,
  Truncated
};

ReceiveExactResult receive_exact(
  int descriptor,
  std::vector<std::uint8_t>& bytes
)
{
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const ssize_t received = ::recv(
      descriptor,
      bytes.data() + offset,
      bytes.size() - offset,
      0
    );
    if (received > 0) {
      offset += static_cast<std::size_t>(received);
      continue;
    }
    if (received == 0) {
      return offset == 0U
        ? ReceiveExactResult::CleanEof
        : ReceiveExactResult::Truncated;
    }
    if (errno == EINTR) {
      continue;
    }
    throw std::system_error(errno, std::generic_category(), "recv");
  }
  return ReceiveExactResult::Complete;
}

void require_valid(int descriptor)
{
  if (descriptor < 0) {
    throw std::logic_error("operation on invalid socket");
  }
}

}  // namespace

Socket::Socket() noexcept : descriptor_{-1}
{
}

Socket::Socket(int descriptor) noexcept : descriptor_{descriptor}
{
}

Socket::~Socket()
{
  close();
}

Socket::Socket(Socket&& other) noexcept
  : descriptor_{std::exchange(other.descriptor_, -1)}
{
}

Socket& Socket::operator=(Socket&& other) noexcept
{
  if (this != &other) {
    close();
    descriptor_ = std::exchange(other.descriptor_, -1);
  }
  return *this;
}

bool Socket::valid() const noexcept
{
  return descriptor_ >= 0;
}

int Socket::native_handle() const noexcept
{
  return descriptor_;
}

void Socket::close() noexcept
{
  if (descriptor_ >= 0) {
    (void)::close(descriptor_);
    descriptor_ = -1;
  }
}

void Socket::send_all(const std::vector<std::uint8_t>& bytes) const
{
  require_valid(descriptor_);
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
#ifdef MSG_NOSIGNAL
    constexpr int FLAGS = MSG_NOSIGNAL;
#else
    constexpr int FLAGS = 0;
#endif
    const ssize_t sent = ::send(
      descriptor_,
      bytes.data() + offset,
      bytes.size() - offset,
      FLAGS
    );
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    const int error = sent == 0 ? EPIPE : errno;
    throw std::system_error(error, std::generic_category(), "send");
  }
}

void Socket::send_record(const Record& record) const
{
  const std::vector<std::uint8_t> internal = serialize_record(record);
  if (internal.empty() || internal.size() > UINT16_MAX) {
    throw std::invalid_argument("record length does not fit prefix");
  }
  const auto length = static_cast<std::uint16_t>(internal.size());
  std::vector<std::uint8_t> external;
  external.reserve(internal.size() + 2U);
  external.push_back(static_cast<std::uint8_t>(length >> 8U));
  external.push_back(static_cast<std::uint8_t>(length));
  external.insert(external.end(), internal.begin(), internal.end());
  send_all(external);
}

std::optional<Record> Socket::receive_record() const
{
  require_valid(descriptor_);
  std::vector<std::uint8_t> prefix(2U);
  const ReceiveExactResult prefix_result = receive_exact(descriptor_, prefix);
  if (prefix_result == ReceiveExactResult::CleanEof) {
    return std::nullopt;
  }
  if (prefix_result == ReceiveExactResult::Truncated) {
    throw std::runtime_error("EOF in record length prefix");
  }

  const std::size_t length =
    (static_cast<std::size_t>(prefix[0U]) << 8U) | prefix[1U];
  if (length == 0U || length > MAX_FRAME_SIZE + 1U) {
    throw std::runtime_error("invalid external record length");
  }

  std::vector<std::uint8_t> internal(length);
  if (receive_exact(descriptor_, internal) != ReceiveExactResult::Complete) {
    throw std::runtime_error("EOF in record body");
  }
  const auto record = parse_record(internal);
  if (!record.has_value()) {
    throw std::runtime_error("malformed application record");
  }
  return record;
}

Socket Socket::accept() const
{
  require_valid(descriptor_);
  while (true) {
    const int accepted = ::accept(descriptor_, nullptr, nullptr);
    if (accepted >= 0) {
      return Socket{accepted};
    }
    if (errno != EINTR) {
      throw std::system_error(errno, std::generic_category(), "accept");
    }
  }
}

std::pair<Socket, Socket> socket_pair()
{
  int descriptors[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
    throw std::system_error(errno, std::generic_category(), "socketpair");
  }
  return {Socket{descriptors[0]}, Socket{descriptors[1]}};
}

Socket listen_tcp(std::uint16_t port, int backlog)
{
  if (backlog <= 0) {
    throw std::invalid_argument("listen backlog must be positive");
  }
  Socket socket{::socket(AF_INET, SOCK_STREAM, 0)};
  if (!socket.valid()) {
    throw std::system_error(errno, std::generic_category(), "socket");
  }
  int reuse = 1;
  if (::setsockopt(
        socket.native_handle(), SOL_SOCKET, SO_REUSEADDR,
        &reuse, sizeof reuse
      ) != 0) {
    throw std::system_error(errno, std::generic_category(), "setsockopt");
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (::bind(
        socket.native_handle(),
        reinterpret_cast<const sockaddr*>(&address),
        sizeof address
      ) != 0) {
    throw std::system_error(errno, std::generic_category(), "bind");
  }
  if (::listen(socket.native_handle(), backlog) != 0) {
    throw std::system_error(errno, std::generic_category(), "listen");
  }
  return socket;
}

Socket connect_tcp(const std::string& host, std::uint16_t port)
{
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  const std::string service = std::to_string(port);
  const int lookup = ::getaddrinfo(
    host.c_str(), service.c_str(), &hints, &addresses
  );
  if (lookup != 0) {
    throw std::runtime_error(::gai_strerror(lookup));
  }

  int last_error = ECONNREFUSED;
  for (addrinfo* address = addresses; address != nullptr;
       address = address->ai_next) {
    Socket socket{::socket(
      address->ai_family, address->ai_socktype, address->ai_protocol
    )};
    if (!socket.valid()) {
      last_error = errno;
      continue;
    }
    if (::connect(
          socket.native_handle(), address->ai_addr, address->ai_addrlen
        ) == 0) {
      ::freeaddrinfo(addresses);
      return socket;
    }
    last_error = errno;
  }
  ::freeaddrinfo(addresses);
  throw std::system_error(last_error, std::generic_category(), "connect");
}

}  // namespace flow_control
