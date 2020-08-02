#pragma once

/**
 * \file libuv/relay.hpp
 */

#include <urn/intrusive_stack.hpp>
#include <urn/relay.hpp>
#include <uv.h>
#include <chrono>
#include <cstdlib>
#include <iostream>


// TODO: http://docs.libuv.org/en/v1.x/udp.html#c.uv_udp_using_recvmmsg


namespace urn_libuv {


inline void die_on_error (int code, const char *fn)
{
  if (code < 0)
  {
    std::cout
      << fn
      << ": "
      << uv_strerror(code)
      << " ("
      << uv_err_name(code)
      << ")\n";
    abort();
  }
}

#define libuv_call(F, ...) die_on_error(F(__VA_ARGS__), #F)


struct config //{{{1
{
  static constexpr std::chrono::seconds statistics_print_interval{5};

  struct
  {
    uint16_t port = 3478;
  } client;

  struct
  {
    uint16_t port = 3479;
  } peer;
};


struct libuv //{{{1
{
  using endpoint = sockaddr;
  struct packet;
  struct client;
  struct peer;
  struct session;
};


struct libuv::packet //{{{1
  : uv_buf_t
{
  packet () = default;

  packet (const uv_buf_t &buf, size_t len) noexcept
    : uv_buf_t(uv_buf_init(buf.base, (int)len))
  { }

  const std::byte *data () const noexcept
  {
    return reinterpret_cast<const std::byte *>(uv_buf_t::base);
  }

  size_t size () const noexcept
  {
    return uv_buf_t::len;
  }
};


struct libuv::client //{{{1
{
  uv_udp_t socket{};

  client (const config &conf) noexcept;

  void start_receive () noexcept
  { }
};


struct libuv::peer //{{{1
{
  uv_udp_t socket{};

  peer (const config &conf) noexcept;

  void start_receive () noexcept
  { }
};


struct libuv::session //{{{1
{
  uv_udp_t socket{};

  session (const endpoint &dest) noexcept;

  void start_send (packet &&p) noexcept;
};


class relay //{{{1
{
public:

  relay (const config &conf) noexcept;

  int run () noexcept
  {
    return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  }

  void on_client_received (
    const sockaddr *src,
    size_t nread,
    const uv_buf_t *buf,
    unsigned flags) noexcept
  {
    if (nread || ((flags & UV_UDP_PARTIAL) != UV_UDP_PARTIAL))
    {
      libuv::packet packet(*buf, nread);
      logic_.on_client_received(*src, packet);
    }
    release_buffer(buf);
  }

  void on_peer_received (
    const sockaddr *src,
    size_t nread,
    const uv_buf_t *buf,
    unsigned flags) noexcept
  {
    if (nread || ((flags & UV_UDP_PARTIAL) != UV_UDP_PARTIAL))
    {
      libuv::packet packet(*buf, nread);
      if (logic_.on_peer_received(*src, std::move(packet)))
      {
        return;
      }
    }
    release_buffer(buf);
  }

  void on_session_sent (libuv::session &session, const libuv::packet &packet)
    noexcept
  {
    logic_.on_session_sent(session, packet);
    release_buffer(&packet);
  }

  void on_statistics_tick () noexcept
  {
    logic_.print_statistics(statistics_interval_);
  }

  const sockaddr *alloc_address () const noexcept
  {
    return &alloc_address_;
  }

  static void alloc_buffer (uv_handle_t *, size_t, uv_buf_t *buf) noexcept
  {
    auto relay = static_cast<urn_libuv::relay *>(uv_default_loop()->data);
    auto block = relay->allocator_.alloc();
    buf->base = block->data;
    buf->len = sizeof(block->data);
  }


private:

  libuv::client client_;
  libuv::peer peer_;
  urn::relay<libuv, false> logic_;
  sockaddr alloc_address_;
  uv_timer_t statistics_timer_;
  const std::chrono::seconds statistics_interval_;

  struct block_pool
  {
    struct block
    {
      union
      {
        struct
        {
          uv_udp_send_t req{};
          libuv::packet packet{};
          libuv::session *session{};
        } session_send{};
      } ctl{};
      urn::intrusive_stack_hook<block> next{};
      char data[8192];
    };
    urn::intrusive_stack<&block::next> pool_{};

    block *alloc () noexcept
    {
      auto b = pool_.try_pop();
      if (!b)
      {
        b = new block;
        if (!b)
        {
          die_on_error(UV_ENOMEM, "allocator::alloc");
        }
      }
      return b;
    }

    void release (block *b) noexcept
    {
      pool_.push(b);
    }

    static block *base_to_block_ptr (char *base) noexcept
    {
      return reinterpret_cast<block *>(
        base + sizeof(block::data) - sizeof(block)
      );
    }
  } allocator_{};

  void release_buffer (const uv_buf_t *buf) noexcept
  {
    allocator_.release(block_pool::base_to_block_ptr(buf->base));
  }

  friend struct libuv::session;
};


} // namespace urn_libuv