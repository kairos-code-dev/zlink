/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"

#if defined ZLINK_HAVE_OPENPGM

#include <new>

#include "transports/pgm/pgm_receiver.hpp"
#include "core/session_base.hpp"
#include "protocol/zmp_decoder.hpp"
#include "utils/stdint.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"

zlink::pgm_receiver_t::pgm_receiver_t (class io_thread_t *parent_,
                                     const options_t &options_) :
    io_object_t (parent_),
    has_rx_timer (false),
    pgm_socket (true, options_),
    options (options_),
    session (NULL),
    active_tsi (NULL),
    insize (0)
{
}

zlink::pgm_receiver_t::~pgm_receiver_t ()
{
    //  Destructor should not be called before unplug.
    zlink_assert (peers.empty ());
}

int zlink::pgm_receiver_t::init (bool udp_encapsulation_, const char *network_)
{
    return pgm_socket.init (udp_encapsulation_, network_);
}

void zlink::pgm_receiver_t::plug (io_thread_t *io_thread_,
                                session_base_t *session_)
{
    LIBZLINK_UNUSED (io_thread_);
    //  Retrieve PGM fds and start polling.
    fd_t socket_fd = retired_fd;
    fd_t waiting_pipe_fd = retired_fd;
    pgm_socket.get_receiver_fds (&socket_fd, &waiting_pipe_fd);
    socket_handle = add_fd (socket_fd);
    pipe_handle = add_fd (waiting_pipe_fd);
    set_pollin (pipe_handle);
    set_pollin (socket_handle);

    session = session_;

    //  If there are any subscriptions already queued in the session, drop them.
    drop_subscriptions ();
}

void zlink::pgm_receiver_t::unplug ()
{
    //  Delete decoders.
    for (peers_t::iterator it = peers.begin (), end = peers.end (); it != end;
         ++it) {
        if (it->second.decoder != NULL) {
            LIBZLINK_DELETE (it->second.decoder);
        }
    }
    peers.clear ();
    active_tsi = NULL;

    if (has_rx_timer) {
        cancel_timer (rx_timer_id);
        has_rx_timer = false;
    }

    rm_fd (socket_handle);
    rm_fd (pipe_handle);

    session = NULL;
}

void zlink::pgm_receiver_t::terminate ()
{
    unplug ();
    delete this;
}

void zlink::pgm_receiver_t::restart_output ()
{
    drop_subscriptions ();
}

bool zlink::pgm_receiver_t::restart_input ()
{
    zlink_assert (session != NULL);
    zlink_assert (active_tsi != NULL);

    const peers_t::iterator it = peers.find (*active_tsi);
    zlink_assert (it != peers.end ());
    zlink_assert (it->second.joined);

    //  Push the pending message into the session.
    int rc = session->push_msg (it->second.decoder->msg ());
    errno_assert (rc == 0);

    if (insize > 0) {
        rc = process_input (it->second.decoder);
        if (rc == -1) {
            //  HWM reached; we will try later.
            if (errno == EAGAIN) {
                session->flush ();
                return true;
            }
            //  Data error. Delete message decoder, mark the
            //  peer as not joined and drop remaining data.
            it->second.joined = false;
            LIBZLINK_DELETE (it->second.decoder);
            insize = 0;
        }
    }

    //  Resume polling.
    set_pollin (pipe_handle);
    set_pollin (socket_handle);

    active_tsi = NULL;
    in_event ();

    return true;
}

const zlink::endpoint_uri_pair_t &zlink::pgm_receiver_t::get_endpoint () const
{
    return _empty_endpoint;
}

void zlink::pgm_receiver_t::in_event ()
{
    // If active_tsi is not null, there is a pending restart_input.
    // Keep the internal state as is so that restart_input would process the right data
    if (active_tsi) {
        return;
    }

    // Read data from the underlying pgm_socket.
    const pgm_tsi_t *tsi = NULL;

    if (has_rx_timer) {
        cancel_timer (rx_timer_id);
        has_rx_timer = false;
    }

    //  Drain as much socket input as possible in one activation.
    //  This minimizes wakeups and improves throughput, but under sustained load
    //  it can monopolize the shared I/O thread (see TODO below).
    //  TODO: This loop can effectively block other engines in the same I/O
    //  thread in the case of high load.
    while (true) {
        //  Get new batch of data.
        //  Note the workaround made not to break strict-aliasing rules.
        insize = 0;
        void *tmp = NULL;
        ssize_t received = pgm_socket.receive (&tmp, &tsi);

        //  No data to process. This may happen if the packet received is
        //  neither ODATA nor ODATA.
        if (received == 0) {
            if (errno == ENOMEM || errno == EBUSY) {
                const long timeout = pgm_socket.get_rx_timeout ();
                add_timer (timeout, rx_timer_id);
                has_rx_timer = true;
            }
            break;
        }

        //  Find the peer based on its TSI.
        peers_t::iterator it = peers.find (*tsi);

        //  Data loss. Delete decoder and mark the peer as disjoint.
        if (received == -1) {
            if (it != peers.end ()) {
                it->second.joined = false;
                if (it->second.decoder != NULL) {
                    LIBZLINK_DELETE (it->second.decoder);
                }
            }
            break;
        }

        //  New peer. Add it to the list of know but unjoint peers.
        if (it == peers.end ()) {
            peer_info_t peer_info = {false, NULL};
            it = peers.ZLINK_MAP_INSERT_OR_EMPLACE (*tsi, peer_info).first;
        }

        insize = static_cast<size_t> (received);
        inpos = (unsigned char *) tmp;

        //  Read the offset of the fist message in the current packet.
        zlink_assert (insize >= sizeof (uint16_t));
        uint16_t offset = get_uint16 (inpos);
        inpos += sizeof (uint16_t);
        insize -= sizeof (uint16_t);

        //  Join the stream if needed.
        if (!it->second.joined) {
            //  There is no beginning of the message in current packet.
            //  Ignore the data.
            if (offset == 0xffff)
                continue;

            zlink_assert (offset <= insize);
            zlink_assert (it->second.decoder == NULL);

            //  We have to move data to the beginning of the first message.
            inpos += offset;
            insize -= offset;

            //  Mark the stream as joined.
            it->second.joined = true;

            //  Create and connect decoder for the peer.
            it->second.decoder = new (std::nothrow)
              zmp_decoder_t (options.in_batch_size, options.maxmsgsize);
            alloc_assert (it->second.decoder);
        }

        int rc = process_input (it->second.decoder);
        if (rc == -1) {
            if (errno == EAGAIN) {
                active_tsi = tsi;

                //  Stop polling.
                reset_pollin (pipe_handle);
                reset_pollin (socket_handle);

                break;
            }

            it->second.joined = false;
            LIBZLINK_DELETE (it->second.decoder);
            insize = 0;
        }
    }

    //  Flush any messages decoder may have produced.
    session->flush ();
}

int zlink::pgm_receiver_t::process_input (zmp_decoder_t *decoder)
{
    zlink_assert (session != NULL);

    while (insize > 0) {
        size_t n = 0;
        int rc = decoder->decode (inpos, insize, n);
        if (rc == -1)
            return -1;
        inpos += n;
        insize -= n;
        if (rc == 0)
            break;
        rc = session->push_msg (decoder->msg ());
        if (rc == -1) {
            errno_assert (errno == EAGAIN);
            return -1;
        }
    }
    return 0;
}


void zlink::pgm_receiver_t::timer_event (int token)
{
    zlink_assert (token == rx_timer_id);

    //  Timer cancels on return by poller_base.
    has_rx_timer = false;
    in_event ();
}

void zlink::pgm_receiver_t::drop_subscriptions ()
{
    msg_t msg;
    msg.init ();
    while (session->pull_msg (&msg) == 0)
        msg.close ();
}

#endif
