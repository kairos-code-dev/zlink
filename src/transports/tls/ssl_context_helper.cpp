/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "transports/tls/ssl_context_helper.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL

#include "engine/asio/asio_debug.hpp"
#include "engine/asio/asio_error_handler.hpp"

#include <boost/asio/buffer.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

namespace zmq
{

std::unique_ptr<boost::asio::ssl::context>
ssl_context_helper_t::create_server_context (const std::string &cert_chain_file,
                                             const std::string &private_key_file,
                                             const std::string &password)
{
    std::unique_ptr<boost::asio::ssl::context> ctx;
    try {
        ctx = std::unique_ptr<boost::asio::ssl::context> (
          new boost::asio::ssl::context (
            boost::asio::ssl::context::tlsv12_server));
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    try {
        //  Set up password callback if needed (thread-safe lambda capture)
        if (!password.empty ()) {
            ctx->set_password_callback (
              [password] (std::size_t max_length,
                          boost::asio::ssl::context::password_purpose) {
                  return password.substr (0, max_length);
              });
        }

        //  Use strong cipher suites only
        SSL_CTX_set_cipher_list (
          ctx->native_handle (),
          "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

        //  Load certificate chain
        ctx->use_certificate_chain_file (cert_chain_file);

        //  Load private key
        ctx->use_private_key_file (private_key_file,
                                   boost::asio::ssl::context::pem);

        return ctx;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("SSL server context creation failed: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return nullptr;
    }
}

std::unique_ptr<boost::asio::ssl::context>
ssl_context_helper_t::create_server_context_from_pem (
  const std::string &cert_chain_pem,
  const std::string &private_key_pem,
  const std::string &password)
{
    std::unique_ptr<boost::asio::ssl::context> ctx;
    try {
        ctx = std::unique_ptr<boost::asio::ssl::context> (
          new boost::asio::ssl::context (
            boost::asio::ssl::context::tlsv12_server));
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    try {
        //  Set up password callback if needed (thread-safe lambda capture)
        if (!password.empty ()) {
            ctx->set_password_callback (
              [password] (std::size_t max_length,
                          boost::asio::ssl::context::password_purpose) {
                  return password.substr (0, max_length);
              });
        }

        //  Use strong cipher suites only
        SSL_CTX_set_cipher_list (
          ctx->native_handle (),
          "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

        //  Load certificate chain from PEM buffer
        ctx->use_certificate_chain (
          boost::asio::buffer (cert_chain_pem.data (), cert_chain_pem.size ()));

        //  Load private key from PEM buffer
        ctx->use_private_key (
          boost::asio::buffer (private_key_pem.data (), private_key_pem.size ()),
          boost::asio::ssl::context::pem);

        return ctx;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("SSL server context creation from PEM failed: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return nullptr;
    }
}

std::unique_ptr<boost::asio::ssl::context>
ssl_context_helper_t::create_client_context (const std::string &ca_cert_file,
                                             bool trust_system,
                                             verification_mode mode)
{
    std::unique_ptr<boost::asio::ssl::context> ctx;
    try {
        ctx = std::unique_ptr<boost::asio::ssl::context> (
          new boost::asio::ssl::context (
            boost::asio::ssl::context::tlsv12_client));
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    try {
        //  Use strong cipher suites only
        SSL_CTX_set_cipher_list (
          ctx->native_handle (),
          "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

        //  Load CA certificate or use system store
        if (!ca_cert_file.empty ()) {
            if (!load_ca_certificate (*ctx, ca_cert_file)) {
                return nullptr;
            }
        } else if (trust_system) {
            //  Use system CA certificates
            ctx->set_default_verify_paths ();
        }

        //  Configure verification
        if (!configure_verification (*ctx, mode)) {
            return nullptr;
        }

        return ctx;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("SSL client context creation failed: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return nullptr;
    }
}

std::unique_ptr<boost::asio::ssl::context>
ssl_context_helper_t::create_client_context_from_pem (
  const std::string &ca_cert_pem, bool trust_system, verification_mode mode)
{
    std::unique_ptr<boost::asio::ssl::context> ctx;
    try {
        ctx = std::unique_ptr<boost::asio::ssl::context> (
          new boost::asio::ssl::context (
            boost::asio::ssl::context::tlsv12_client));
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    try {
        //  Use strong cipher suites only
        SSL_CTX_set_cipher_list (
          ctx->native_handle (),
          "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

        //  Load CA certificate from PEM or system store
        if (!ca_cert_pem.empty ()) {
            if (!load_ca_certificate_from_pem (*ctx, ca_cert_pem)) {
                return nullptr;
            }
        } else if (trust_system) {
            ctx->set_default_verify_paths ();
        }

        //  Configure verification
        if (!configure_verification (*ctx, mode)) {
            return nullptr;
        }

        return ctx;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("SSL client context creation from PEM failed: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return nullptr;
    }
}

std::unique_ptr<boost::asio::ssl::context>
ssl_context_helper_t::create_client_context_with_cert (
  const std::string &ca_cert_file,
  const std::string &client_cert_file,
  const std::string &client_key_file,
  const std::string &password,
  bool trust_system,
  verification_mode mode)
{
    std::unique_ptr<boost::asio::ssl::context> ctx;
    try {
        ctx = std::unique_ptr<boost::asio::ssl::context> (
          new boost::asio::ssl::context (
            boost::asio::ssl::context::tlsv12_client));
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    try {
        //  Set up password callback if needed (thread-safe lambda capture)
        if (!password.empty ()) {
            ctx->set_password_callback (
              [password] (std::size_t max_length,
                          boost::asio::ssl::context::password_purpose) {
                  return password.substr (0, max_length);
              });
        }

        //  Use strong cipher suites only
        SSL_CTX_set_cipher_list (
          ctx->native_handle (),
          "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

        //  Load CA certificate or use system store
        if (!ca_cert_file.empty ()) {
            if (!load_ca_certificate (*ctx, ca_cert_file)) {
                return nullptr;
            }
        } else if (trust_system) {
            ctx->set_default_verify_paths ();
        }

        //  Load client certificate
        ctx->use_certificate_chain_file (client_cert_file);

        //  Load client private key
        ctx->use_private_key_file (client_key_file,
                                   boost::asio::ssl::context::pem);

        //  Configure verification
        if (!configure_verification (*ctx, mode)) {
            return nullptr;
        }

        return ctx;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("SSL client context with cert creation failed: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return nullptr;
    }
}

std::unique_ptr<boost::asio::ssl::context>
ssl_context_helper_t::create_client_context_with_cert_from_pem (
  const std::string &ca_cert_pem,
  const std::string &client_cert_pem,
  const std::string &client_key_pem,
  const std::string &password,
  bool trust_system,
  verification_mode mode)
{
    std::unique_ptr<boost::asio::ssl::context> ctx;
    try {
        ctx = std::unique_ptr<boost::asio::ssl::context> (
          new boost::asio::ssl::context (
            boost::asio::ssl::context::tlsv12_client));
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    try {
        //  Set up password callback if needed (thread-safe lambda capture)
        if (!password.empty ()) {
            ctx->set_password_callback (
              [password] (std::size_t max_length,
                          boost::asio::ssl::context::password_purpose) {
                  return password.substr (0, max_length);
              });
        }

        //  Use strong cipher suites only
        SSL_CTX_set_cipher_list (
          ctx->native_handle (),
          "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

        //  Load CA certificate from PEM or system store
        if (!ca_cert_pem.empty ()) {
            if (!load_ca_certificate_from_pem (*ctx, ca_cert_pem)) {
                return nullptr;
            }
        } else if (trust_system) {
            ctx->set_default_verify_paths ();
        }

        //  Load client certificate from PEM buffer
        ctx->use_certificate_chain (
          boost::asio::buffer (client_cert_pem.data (),
                               client_cert_pem.size ()));

        //  Load client private key from PEM buffer
        ctx->use_private_key (
          boost::asio::buffer (client_key_pem.data (), client_key_pem.size ()),
          boost::asio::ssl::context::pem);

        //  Configure verification
        if (!configure_verification (*ctx, mode)) {
            return nullptr;
        }

        return ctx;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR (
          "SSL client context with cert from PEM creation failed: %s",
          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return nullptr;
    }
}

bool ssl_context_helper_t::configure_verification (
  boost::asio::ssl::context &ctx, verification_mode mode)
{
    try {
        switch (mode) {
            case verify_none:
                ctx.set_verify_mode (boost::asio::ssl::verify_none);
                break;

            case verify_peer:
                ctx.set_verify_mode (boost::asio::ssl::verify_peer);
                break;

            case verify_fail:
                ctx.set_verify_mode (boost::asio::ssl::verify_peer
                                     | boost::asio::ssl::verify_fail_if_no_peer_cert);
                break;

            case verify_client:
                ctx.set_verify_mode (boost::asio::ssl::verify_peer
                                     | boost::asio::ssl::verify_fail_if_no_peer_cert
                                     | boost::asio::ssl::verify_client_once);
                break;

            default:
                return false;
        }
        return true;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("SSL verification configuration failed: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return false;
    }
}

bool ssl_context_helper_t::load_ca_certificate (boost::asio::ssl::context &ctx,
                                                const std::string &ca_cert_file)
{
    try {
        ctx.load_verify_file (ca_cert_file);
        return true;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("Failed to load CA certificate from file: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return false;
    }
}

bool ssl_context_helper_t::load_ca_certificate_from_pem (
  boost::asio::ssl::context &ctx, const std::string &ca_cert_pem)
{
    try {
        ctx.add_certificate_authority (
          boost::asio::buffer (ca_cert_pem.data (), ca_cert_pem.size ()));
        return true;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("Failed to load CA certificate from PEM: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return false;
    }
}

bool ssl_context_helper_t::set_hostname_verification (
  boost::asio::ssl::context &ctx, const std::string &hostname)
{
    if (hostname.empty ()) {
        ASIO_GLOBAL_ERROR ("set_hostname_verification: hostname is empty");
        return false;
    }

    //  Get the native SSL_CTX handle
    SSL_CTX *native_ctx = ctx.native_handle ();

    //  Enable hostname verification using OpenSSL's built-in support
    //  This is supported in OpenSSL 1.0.2+ and uses X509_VERIFY_PARAM
    X509_VERIFY_PARAM *param = SSL_CTX_get0_param (native_ctx);
    if (!param) {
        ASIO_GLOBAL_ERROR (
          "set_hostname_verification: failed to get X509_VERIFY_PARAM");
        return false;
    }

    //  Enable hostname checking (flags for DNS name verification)
    X509_VERIFY_PARAM_set_hostflags (param,
                                     X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    //  Set the expected hostname for verification
    if (!X509_VERIFY_PARAM_set1_host (param, hostname.c_str (),
                                      hostname.size ())) {
        ASIO_GLOBAL_ERROR (
          "set_hostname_verification: failed to set hostname '%s'",
          hostname.c_str ());
        return false;
    }

    return true;
}

bool ssl_context_helper_t::configure_server_verification (
  boost::asio::ssl::context &ctx, bool require_client_cert)
{
    try {
        if (require_client_cert) {
            //  mTLS mode: require and verify client certificate
            ctx.set_verify_mode (boost::asio::ssl::verify_peer
                                 | boost::asio::ssl::verify_fail_if_no_peer_cert
                                 | boost::asio::ssl::verify_client_once);
        } else {
            //  TLS mode: do not require client certificate
            ctx.set_verify_mode (boost::asio::ssl::verify_none);
        }
        return true;
    }
    catch (const boost::system::system_error &e) {
        ASIO_GLOBAL_ERROR ("Failed to configure server verification: %s",
                          e.what ());
        (void) e;  // Suppress unused variable warning when debug is disabled
        return false;
    }
}

std::string ssl_context_helper_t::get_ssl_error_string ()
{
    unsigned long err = ERR_get_error ();
    if (err == 0)
        return std::string ();

    char buf[256];
    ERR_error_string_n (err, buf, sizeof (buf));
    return std::string (buf);
}

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_SSL
