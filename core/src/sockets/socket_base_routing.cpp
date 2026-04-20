/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"

#include "utils/err.hpp"
#include "utils/macros.hpp"
#include <cstdio>
#include <cstdlib>

namespace
{
bool routing_map_debug_enabled ()
{
    return std::getenv ("ZLINK_ROUTER_DEBUG") != NULL;
}

void format_blob_debug (const zlink::blob_t &blob_, char *buf_, size_t size_)
{
    if (!buf_ || size_ == 0) {
        return;
    }

    if (blob_.size () == 0) {
        std::snprintf (buf_, size_, "<empty>");
        return;
    }

    size_t used = 0;
    for (size_t i = 0; i < blob_.size () && used + 4 < size_; ++i) {
        const unsigned char c = blob_.data ()[i];
        const int rc = std::snprintf (buf_ + used, size_ - used, "%c%02X",
                                      (c >= 32 && c <= 126)
                                        ? static_cast<char> (c)
                                        : '.',
                                      static_cast<unsigned> (c));
        if (rc <= 0)
            break;
        used += static_cast<size_t> (rc);
        if (i + 1 < blob_.size () && used + 2 < size_) {
            buf_[used++] = ' ';
            buf_[used] = '\0';
        }
    }
}
}

zlink::routing_socket_base_t::routing_socket_base_t (class ctx_t *parent_,
                                                     uint32_t tid_,
                                                     int sid_) :
    socket_base_t (parent_, tid_, sid_)
{
}

zlink::routing_socket_base_t::~routing_socket_base_t ()
{
    zlink_assert (_out_pipes.empty ());
}

int zlink::routing_socket_base_t::xsetsockopt (int option_,
                                               const void *optval_,
                                               size_t optvallen_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID:
            // TODO why isn't it possible to set an empty connect_routing_id
            //   (which is the default value)
            if (optval_ && optvallen_) {
                _connect_routing_id.assign (static_cast<const char *> (optval_),
                                            optvallen_);
                return 0;
            }
            break;
    }
    errno = EINVAL;
    return -1;
}

void zlink::routing_socket_base_t::xwrite_activated (pipe_t *pipe_)
{
    const out_pipes_t::iterator end = _out_pipes.end ();
    out_pipes_t::iterator it;
    for (it = _out_pipes.begin (); it != end; ++it)
        if (it->second.pipe == pipe_)
            break;

    zlink_assert (it != end);
    // Duplicate write-activation notifications can race with async flush
    // cycles under high STREAM load. Keep activation idempotent.
    if (it->second.active)
        return;
    it->second.active = true;
}

std::string zlink::routing_socket_base_t::extract_connect_routing_id ()
{
    std::string res = ZLINK_MOVE (_connect_routing_id);
    _connect_routing_id.clear ();
    return res;
}

bool zlink::routing_socket_base_t::connect_routing_id_is_set () const
{
    return !_connect_routing_id.empty ();
}

void zlink::routing_socket_base_t::add_out_pipe (blob_t routing_id_,
                                                 pipe_t *pipe_)
{
    if (routing_map_debug_enabled ()) {
        char rid_text[160];
        format_blob_debug (routing_id_, rid_text, sizeof (rid_text));
        std::fprintf (stderr,
                      "routing map add: pipe=%p rid_size=%zu rid=%s\n",
                      static_cast<void *> (pipe_), routing_id_.size (),
                      rid_text);
    }
    const out_pipe_t outpipe = {pipe_, true, ZLINK_ADMISSION_SERVING};
    const bool ok =
      _out_pipes.ZLINK_MAP_INSERT_OR_EMPLACE (ZLINK_MOVE (routing_id_), outpipe)
        .second;
    zlink_assert (ok);
}

bool zlink::routing_socket_base_t::has_out_pipe (const blob_t &routing_id_) const
{
    return 0 != _out_pipes.count (routing_id_);
}

zlink::routing_socket_base_t::out_pipe_t *
zlink::routing_socket_base_t::lookup_out_pipe (const blob_t &routing_id_)
{
    const out_pipes_t::iterator it = _out_pipes.find (routing_id_);
    if (it != _out_pipes.end ()) {
#if !defined _MSC_VER
        __builtin_prefetch (&it->second, 0, 3);
#endif
        return &it->second;
    }
    if (routing_map_debug_enabled ()) {
        char rid_text[160];
        format_blob_debug (routing_id_, rid_text, sizeof (rid_text));
        std::fprintf (stderr,
                      "routing map miss: rid_size=%zu rid=%s entries=%zu\n",
                      routing_id_.size (), rid_text, _out_pipes.size ());
        for (out_pipes_t::const_iterator dump = _out_pipes.begin (),
                                         end = _out_pipes.end ();
             dump != end; ++dump) {
            char entry_text[160];
            format_blob_debug (dump->first, entry_text, sizeof (entry_text));
            std::fprintf (stderr,
                          "  routing map entry: pipe=%p rid_size=%zu rid=%s\n",
                          static_cast<void *> (dump->second.pipe),
                          dump->first.size (), entry_text);
        }
    }
    return NULL;
}

const zlink::routing_socket_base_t::out_pipe_t *
zlink::routing_socket_base_t::lookup_out_pipe (const blob_t &routing_id_) const
{
    const out_pipes_t::const_iterator it = _out_pipes.find (routing_id_);
    if (it != _out_pipes.end ()) {
#if !defined _MSC_VER
        __builtin_prefetch (&it->second, 0, 3);
#endif
        return &it->second;
    }
    return NULL;
}

void zlink::routing_socket_base_t::erase_out_pipe (const pipe_t *pipe_)
{
    if (!pipe_)
        return;

    const size_t erased = _out_pipes.erase (pipe_->get_routing_id ());
    if (erased != 0)
        return;

    // Routing id may have been refreshed after attach. Fall back to
    // pointer-based lookup to keep teardown idempotent and avoid stale pipes.
    for (out_pipes_t::iterator it = _out_pipes.begin (),
                               end = _out_pipes.end ();
         it != end; ++it) {
        if (it->second.pipe == pipe_) {
            _out_pipes.erase (it);
            return;
        }
    }
}

zlink::routing_socket_base_t::out_pipe_t
zlink::routing_socket_base_t::try_erase_out_pipe (const blob_t &routing_id_)
{
    const out_pipes_t::iterator it = _out_pipes.find (routing_id_);
    out_pipe_t res = {NULL, false};
    if (it != _out_pipes.end ()) {
        res = it->second;
        _out_pipes.erase (it);
    }
    return res;
}
