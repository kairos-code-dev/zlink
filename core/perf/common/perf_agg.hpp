#ifndef PERF_AGG_HPP
#define PERF_AGG_HPP

// Aggregate send/recv helpers built on *_part primitives.
// These replicate the removed aggregate API for perf benchmarks
// that cannot access core internal headers.

#include <zlink.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace perf_agg {

struct recv_storage_t
{
    recv_storage_t () : parts (NULL), count (0), capacity (0) {}

    zlink_msg_t *parts;
    size_t count;
    size_t capacity;
};

inline recv_storage_t &recv_storage ()
{
    static thread_local recv_storage_t storage;
    return storage;
}

inline void reset_recv_storage ()
{
    recv_storage_t &storage = recv_storage ();
    for (size_t i = 0; i < storage.count; ++i)
        zlink_msg_close (&storage.parts[i]);
    storage.count = 0;
}

inline int ensure_recv_capacity (size_t required_)
{
    recv_storage_t &storage = recv_storage ();
    if (required_ <= storage.capacity)
        return 0;

    const size_t new_capacity =
      std::max (required_, storage.capacity > 0 ? storage.capacity * 2 : 4U);
    zlink_msg_t *new_parts = static_cast<zlink_msg_t *> (
      std::malloc (new_capacity * sizeof (zlink_msg_t)));
    if (!new_parts) {
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < storage.count; ++i) {
        zlink_msg_init (&new_parts[i]);
        if (zlink_msg_move (&new_parts[i], &storage.parts[i]) == 0)
            continue;

        for (size_t j = 0; j <= i; ++j)
            zlink_msg_close (&new_parts[j]);
        std::free (new_parts);
        return -1;
    }

    std::free (storage.parts);
    storage.parts = new_parts;
    storage.capacity = new_capacity;
    return 0;
}

inline int append_recv_part (zlink_msg_t *part_)
{
    if (!part_) {
        errno = EFAULT;
        return -1;
    }

    recv_storage_t &storage = recv_storage ();
    if (ensure_recv_capacity (storage.count + 1) != 0)
        return -1;

    zlink_msg_init (&storage.parts[storage.count]);
    if (zlink_msg_move (&storage.parts[storage.count], part_) != 0) {
        zlink_msg_close (&storage.parts[storage.count]);
        return -1;
    }

    ++storage.count;
    return 0;
}

inline int collect_recv_parts (void *s_,
                               zlink_msg_t **parts_out_,
                               size_t *count_out_,
                               zlink_msg_t *first_,
                               zlink_part_flag_t has_more_)
{
    if (!parts_out_ || !count_out_ || !first_) {
        errno = EFAULT;
        return -1;
    }

    reset_recv_storage ();
    if (append_recv_part (first_) != 0) {
        zlink_msg_close (first_);
        reset_recv_storage ();
        return -1;
    }

    while (has_more_) {
        const zlink_routing_id_t *dummy = NULL;
        zlink_msg_t part;
        zlink_msg_init (&part);
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc =
          zlink_recv_part (s_, &dummy, &part, &more,
                           static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close (&part);
            reset_recv_storage ();
            return -1;
        }
        if (append_recv_part (&part) != 0) {
            zlink_msg_close (&part);
            reset_recv_storage ();
            return -1;
        }
        has_more_ = more;
    }

    recv_storage_t &storage = recv_storage ();
    *parts_out_ = storage.parts;
    *count_out_ = storage.count;
    return 0;
}

inline void multipart_close (zlink_msg_t *parts_, size_t count_)
{
    recv_storage_t &storage = recv_storage ();
    if (parts_ == storage.parts && count_ == storage.count) {
        reset_recv_storage ();
        return;
    }

    zlink_multipart_close (parts_, count_);
}

} // namespace perf_agg

inline zlink_recv_result_t zlink_recv (void *s_,
                                        zlink_routing_id_t *source_rid_out_,
                                        zlink_msg_t **parts_out_,
                                        size_t *count_out_,
                                        zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }
    const zlink_routing_id_t *rid_ptr = NULL;
    zlink_msg_t first;
    zlink_msg_init (&first);
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_recv_result_t rc =
      zlink_recv_part (s_, &rid_ptr, &first, &has_more, flags_);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&first);
        return rc;
    }
    if (source_rid_out_ && rid_ptr)
        *source_rid_out_ = *rid_ptr;
    return perf_agg::collect_recv_parts (s_, parts_out_, count_out_,
                                         &first, has_more) == 0
             ? ZLINK_RECV_OK
             : ZLINK_RECV_INTERNAL_ERROR;
}

inline zlink_submit_result_t zlink_send (void *s_,
                                          zlink_msg_t *parts_,
                                          size_t count_,
                                          zlink_send_flags_t flags_)
{
    if (!parts_ || count_ == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count_; ++i) {
        const zlink_part_flag_t f =
          i + 1 < count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const zlink_submit_result_t rc =
          zlink_send_part (s_, &parts_[i], flags_, f);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1; j < count_; ++j)
                zlink_msg_close (&parts_[j]);
            return rc;
        }
    }
    return ZLINK_SUBMIT_OK;
}

inline zlink_submit_result_t zlink_send_rid (void *s_,
                                              const zlink_routing_id_t *rid_,
                                              zlink_msg_t *parts_,
                                              size_t count_,
                                              zlink_send_flags_t flags_)
{
    if (!parts_ || count_ == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count_; ++i) {
        const zlink_part_flag_t f =
          i + 1 < count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const zlink_submit_result_t rc =
          zlink_send_part_rid (s_, rid_, &parts_[i], flags_, f);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1; j < count_; ++j)
                zlink_msg_close (&parts_[j]);
            return rc;
        }
    }
    return ZLINK_SUBMIT_OK;
}

inline zlink_recv_result_t zlink_subscribe (void *sub_,
                                             zlink_routing_id_t *source_rid_out_,
                                             zlink_msg_t **parts_out_,
                                             size_t *count_out_,
                                             char *topic_out_,
                                             size_t *topic_len_out_,
                                             zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }
    const zlink_routing_id_t *rid_ptr = NULL;
    const size_t topic_cap = topic_len_out_ ? *topic_len_out_ : 0;
    zlink_msg_t first;
    zlink_msg_init (&first);
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_recv_result_t rc =
      zlink_subscribe_part (sub_, &rid_ptr, topic_out_, topic_cap,
                            topic_len_out_, &first, &has_more, flags_);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&first);
        return rc;
    }
    if (source_rid_out_ && rid_ptr)
        *source_rid_out_ = *rid_ptr;
    return perf_agg::collect_recv_parts (sub_, parts_out_, count_out_,
                                         &first, has_more) == 0
             ? ZLINK_RECV_OK
             : ZLINK_RECV_INTERNAL_ERROR;
}

inline zlink_submit_result_t zlink_publish (void *pub_,
                                             const char *topic_,
                                             zlink_msg_t *parts_,
                                             size_t count_,
                                             zlink_send_flags_t flags_)
{
    if (!parts_ || count_ == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count_; ++i) {
        const zlink_part_flag_t f =
          i + 1 < count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const zlink_submit_result_t rc =
          zlink_publish_part (pub_, topic_, &parts_[i], flags_, f);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1; j < count_; ++j)
                zlink_msg_close (&parts_[j]);
            return rc;
        }
    }
    return ZLINK_SUBMIT_OK;
}

inline zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *count_out_,
  zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }
    zlink_msg_t first;
    zlink_msg_init (&first);
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_recv_result_t rc =
      zlink_router_recv_part (router_, source_node_rid_out_,
                              source_spot_rid_out_, request_seq_out_, &first,
                              &has_more, flags_);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&first);
        return rc;
    }
    return perf_agg::collect_recv_parts (router_, parts_out_, count_out_,
                                         &first, has_more) == 0
             ? ZLINK_RECV_OK
             : ZLINK_RECV_INTERNAL_ERROR;
}

#endif
