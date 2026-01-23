/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_RAW_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_RAW_ENGINE_HPP_INCLUDED__

#include "engine/asio/asio_engine.hpp"
#if defined ZMQ_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif

namespace zmq
{
class asio_raw_engine_t ZMQ_FINAL : public asio_engine_t
{
  public:
    asio_raw_engine_t (fd_t fd_,
                       const options_t &options_,
                       const endpoint_uri_pair_t &endpoint_uri_pair_);
    asio_raw_engine_t (fd_t fd_,
                       const options_t &options_,
                       const endpoint_uri_pair_t &endpoint_uri_pair_,
                       std::unique_ptr<i_asio_transport> transport_);
#if defined ZMQ_HAVE_ASIO_SSL
    asio_raw_engine_t (fd_t fd_,
                       const options_t &options_,
                       const endpoint_uri_pair_t &endpoint_uri_pair_,
                       std::unique_ptr<i_asio_transport> transport_,
                       std::unique_ptr<boost::asio::ssl::context> ssl_context_);
#endif
    ~asio_raw_engine_t () ZMQ_OVERRIDE;

  protected:
    void plug_internal () ZMQ_OVERRIDE;
    bool build_gather_header (const msg_t &msg_,
                              unsigned char *buffer_,
                              size_t buffer_size_,
                              size_t &header_size_) ZMQ_OVERRIDE;

  private:
    void init_raw_engine ();

#if defined ZMQ_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_raw_engine_t)
};
}

#endif
