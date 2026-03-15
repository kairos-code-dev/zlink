/* SPDX-License-Identifier: MPL-2.0 */
#include "testutil_monitoring.hpp"
#include "../src/core/recv_internal.hpp"
#include "testutil_unity.hpp"

#include <stdlib.h>
#include <string.h>

static bool read_monitor_u64_part (const zlink_msg_t *part_,
                                   uint64_t *value_out_)
{
    if (!part_ || !value_out_ || zlink_msg_size (part_) < sizeof (uint64_t))
        return false;

    memcpy (value_out_, zlink_msg_data (const_cast<zlink_msg_t *> (part_)),
            sizeof (uint64_t));
    return true;
}

int recv_monitor_event_from_socket (void *monitor_,
                                    zlink_monitor_event_t *event_,
                                    int flags_)
{
    if (!monitor_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t parts[16];
    size_t part_count = 0;
    bool more = true;
    while (more && part_count < sizeof (parts) / sizeof (parts[0])) {
        zlink_msg_init (&parts[part_count]);
        const int rc = zlink::recv_msg_internal (
          monitor_, &parts[part_count],
          part_count == 0 ? flags_ : (flags_ & ~ZLINK_DONTWAIT));
        if (rc < 0) {
            zlink_msg_close (&parts[part_count]);
            for (size_t i = 0; i < part_count; ++i)
                zlink_msg_close (&parts[i]);
            return -1;
        }
        more = zlink_msg_more (&parts[part_count]) != 0;
        ++part_count;
    }

    if (part_count == 1 && zlink_msg_size (&parts[0]) == sizeof (*event_)) {
        memcpy (event_, zlink_msg_data (&parts[0]), sizeof (*event_));
        zlink_msg_close (&parts[0]);
        return 0;
    }

    if (more || part_count < 5) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    memset (event_, 0, sizeof (*event_));

    uint64_t value_count = 0;
    if (!read_monitor_u64_part (&parts[0], &event_->event)
        || !read_monitor_u64_part (&parts[1], &value_count)) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    const size_t expected_part_count = static_cast<size_t> (value_count) + 5;
    if (part_count != expected_part_count) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    if (value_count > 0
        && !read_monitor_u64_part (&parts[2], &event_->value)) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    const size_t routing_id_index = static_cast<size_t> (value_count) + 2;
    const size_t routing_id_size = zlink_msg_size (&parts[routing_id_index]);
    const size_t routing_id_copy =
      routing_id_size > sizeof (event_->routing_id.data)
        ? sizeof (event_->routing_id.data)
        : routing_id_size;
    event_->routing_id.size = static_cast<uint8_t> (routing_id_copy);
    if (routing_id_copy > 0) {
        memcpy (event_->routing_id.data,
                zlink_msg_data (&parts[routing_id_index]), routing_id_copy);
    }

    const size_t local_index = routing_id_index + 1;
    const size_t local_size = zlink_msg_size (&parts[local_index]);
    const size_t local_copy =
      local_size >= sizeof (event_->local_addr)
        ? sizeof (event_->local_addr) - 1
        : local_size;
    if (local_copy > 0)
        memcpy (event_->local_addr, zlink_msg_data (&parts[local_index]),
                local_copy);
    event_->local_addr[local_copy] = '\0';

    const size_t remote_index = local_index + 1;
    const size_t remote_size = zlink_msg_size (&parts[remote_index]);
    const size_t remote_copy =
      remote_size >= sizeof (event_->remote_addr)
        ? sizeof (event_->remote_addr) - 1
        : remote_size;
    if (remote_copy > 0)
        memcpy (event_->remote_addr, zlink_msg_data (&parts[remote_index]),
                remote_copy);
    event_->remote_addr[remote_copy] = '\0';

    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
    return 0;
}

int recv_service_event_from_socket (void *monitor_,
                                    zlink_service_event_t *event_,
                                    int flags_)
{
    if (!monitor_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    const int rc = zlink::recv_msg_internal (monitor_, &msg, flags_);
    if (rc < 0) {
        zlink_msg_close (&msg);
        return -1;
    }
    if (zlink_msg_size (&msg) != sizeof (*event_)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }
    memcpy (event_, zlink_msg_data (&msg), sizeof (*event_));
    zlink_msg_close (&msg);
    return 0;
}

//  Read one event off the monitor socket; return value and address
//  by reference, if not null, and event number by value. Returns -1
//  in case of error.
static int get_monitor_event_internal (void *monitor_,
                                       int *value_,
                                       char **address_,
                                       int recv_flag_)
{
    zlink_monitor_event_t event;
    if (recv_monitor_event_from_socket (monitor_, &event, recv_flag_) == -1) {
        TEST_ASSERT_FAILURE_ERRNO (EAGAIN, -1);
        return -1;
    }

    if (value_)
        *value_ = static_cast<int> (event.value);

    if (address_) {
        const char *addr = event.remote_addr[0] ? event.remote_addr
                                                : event.local_addr;
        const size_t len = strlen (addr);
        *address_ = static_cast<char *> (malloc (len + 1));
        memcpy (*address_, addr, len);
        (*address_)[len] = '\0';
    }

    return static_cast<int> (event.event);
}

int get_monitor_event_with_timeout (void *monitor_,
                                    int *value_,
                                    char **address_,
                                    int timeout_)
{
    int res;
    if (timeout_ == -1) {
        int timeout_step = 250;
        int wait_time = 0;
        while (true) {
            if (zlink::wait_socket_events_internal (monitor_, 1, timeout_step)
                <= 0) {
                wait_time += timeout_step;
                fprintf (stderr, "Still waiting for monitor event after %i ms\n",
                         wait_time);
                continue;
            }
            res = get_monitor_event_internal (monitor_, value_, address_, 0);
            if (res != -1)
                break;
            wait_time += timeout_step;
            fprintf (stderr, "Still waiting for monitor event after %i ms\n",
                     wait_time);
        }
    } else {
        if (zlink::wait_socket_events_internal (monitor_, 1, timeout_) <= 0) {
            errno = EAGAIN;
            return -1;
        }
        res = get_monitor_event_internal (monitor_, value_, address_, 0);
    }
    return res;
}

int get_monitor_event (void *monitor_, int *value_, char **address_)
{
    return get_monitor_event_with_timeout (monitor_, value_, address_, -1);
}

void expect_monitor_event (void *monitor_, int expected_event_)
{
    TEST_ASSERT_EQUAL_HEX (expected_event_,
                           get_monitor_event (monitor_, NULL, NULL));
}

static void print_unexpected_event (char *buf_,
                                    size_t buf_size_,
                                    int event_,
                                    int err_,
                                    int expected_event_,
                                    int expected_err_)
{
    snprintf (buf_, buf_size_,
              "Unexpected event: 0x%x, value = %i/0x%x (expected: 0x%x, value "
              "= %i/0x%x)\n",
              event_, err_, err_, expected_event_, expected_err_,
              expected_err_);
}

void print_unexpected_event_stderr (int event_,
                                    int err_,
                                    int expected_event_,
                                    int expected_err_)
{
    char buf[256];
    print_unexpected_event (buf, sizeof buf, event_, err_, expected_event_,
                            expected_err_);
    fputs (buf, stderr);
}

int expect_monitor_event_multiple (void *server_mon_,
                                   int expected_event_,
                                   int expected_err_,
                                   bool optional_)
{
    int count_of_expected_events = 0;
    int client_closed_connection = 0;
    int timeout = 250;
    int wait_time = 0;

    int event;
    int err;
    while ((event =
              get_monitor_event_with_timeout (server_mon_, &err, NULL, timeout))
             != -1
           || !count_of_expected_events) {
        if (event == -1) {
            if (optional_)
                break;
            wait_time += timeout;
            fprintf (stderr,
                     "Still waiting for first event after %ims (expected event "
                     "%x (value %i/0x%x))\n",
                     wait_time, expected_event_, expected_err_, expected_err_);
            continue;
        }
        // ignore errors with EPIPE/ECONNRESET/ECONNABORTED, which can happen
        // ECONNRESET can happen on very slow machines, when the engine writes
        // to the peer and then tries to read the socket before the peer reads
        // ECONNABORTED happens when a client aborts a connection via RST/timeout
        if (event == ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
            && ((err == EPIPE && expected_err_ != EPIPE) || err == ECONNRESET
                || err == ECONNABORTED)) {
            fprintf (stderr,
                     "Ignored event (skipping any further events): %x (err = "
                     "%i == %s)\n",
                     event, err, zlink_strerror (err));
            client_closed_connection = 1;
            break;
        }
        if (event != expected_event_
            || (-1 != expected_err_ && err != expected_err_)) {
            char buf[256];
            print_unexpected_event (buf, sizeof buf, event, err,
                                    expected_event_, expected_err_);
            TEST_FAIL_MESSAGE (buf);
        }
        ++count_of_expected_events;
    }
    TEST_ASSERT_TRUE (optional_ || count_of_expected_events > 0
                      || client_closed_connection);

    return count_of_expected_events;
}

static int64_t get_monitor_event_internal_v2 (void *monitor_,
                                              uint64_t **value_,
                                              char **local_address_,
                                              char **remote_address_,
                                              int recv_flag_)
{
    zlink_monitor_event_t event;
    if (recv_monitor_event_from_socket (monitor_, &event, recv_flag_) == -1) {
        TEST_ASSERT_FAILURE_ERRNO (EAGAIN, -1);
        return -1;
    }

    if (value_) {
        *value_ = static_cast<uint64_t *> (malloc (sizeof (uint64_t)));
        TEST_ASSERT_NOT_NULL (*value_);
        (*value_)[0] = event.value;
    }

    if (local_address_) {
        const size_t len = strlen (event.local_addr);
        *local_address_ = static_cast<char *> (malloc (len + 1));
        memcpy (*local_address_, event.local_addr, len);
        (*local_address_)[len] = '\0';
    }

    if (remote_address_) {
        const size_t len = strlen (event.remote_addr);
        *remote_address_ = static_cast<char *> (malloc (len + 1));
        memcpy (*remote_address_, event.remote_addr, len);
        (*remote_address_)[len] = '\0';
    }

    return static_cast<int64_t> (event.event);
}

static int64_t get_monitor_event_with_timeout_v2 (void *monitor_,
                                                  uint64_t **value_,
                                                  char **local_address_,
                                                  char **remote_address_,
                                                  int timeout_)
{
    int64_t res;
    if (timeout_ == -1) {
        int timeout_step = 250;
        int wait_time = 0;
        while (true) {
            if (zlink::wait_socket_events_internal (monitor_, 1, timeout_step)
                <= 0) {
                wait_time += timeout_step;
                fprintf (stderr, "Still waiting for monitor event after %i ms\n",
                         wait_time);
                continue;
            }
            res = get_monitor_event_internal_v2 (
              monitor_, value_, local_address_, remote_address_, 0);
            if (res != -1)
                break;
            wait_time += timeout_step;
            fprintf (stderr, "Still waiting for monitor event after %i ms\n",
                     wait_time);
        }
    } else {
        if (zlink::wait_socket_events_internal (monitor_, 1, timeout_) <= 0) {
            errno = EAGAIN;
            return -1;
        }
        res = get_monitor_event_internal_v2 (monitor_, value_, local_address_,
                                             remote_address_, 0);
    }
    return res;
}

int64_t get_monitor_event_v2 (void *monitor_,
                              uint64_t **value_,
                              char **local_address_,
                              char **remote_address_)
{
    return get_monitor_event_with_timeout_v2 (monitor_, value_, local_address_,
                                              remote_address_, -1);
}

void expect_monitor_event_v2 (void *monitor_,
                              int64_t expected_event_,
                              const char *expected_local_address_,
                              const char *expected_remote_address_)
{
    char *local_address = NULL;
    char *remote_address = NULL;
    int64_t event = get_monitor_event_v2 (
      monitor_, NULL, expected_local_address_ ? &local_address : NULL,
      expected_remote_address_ ? &remote_address : NULL);
    bool failed = false;
    char buf[256];
    char *pos = buf;
    if (event != expected_event_) {
        pos += snprintf (pos, sizeof buf - (pos - buf),
                         "Expected monitor event %llx, but received %llx\n",
                         static_cast<long long> (expected_event_),
                         static_cast<long long> (event));
        failed = true;
    }
    if (expected_local_address_
        && 0 != strcmp (local_address, expected_local_address_)) {
        pos += snprintf (pos, sizeof buf - (pos - buf),
                         "Expected local address %s, but received %s\n",
                         expected_local_address_, local_address);
    }
    if (expected_remote_address_
        && 0 != strcmp (remote_address, expected_remote_address_)) {
        snprintf (pos, sizeof buf - (pos - buf),
                  "Expected remote address %s, but received %s\n",
                  expected_remote_address_, remote_address);
    }
    free (local_address);
    free (remote_address);
    TEST_ASSERT_FALSE_MESSAGE (failed, buf);
}


const char *get_zlinkEventName (uint64_t event)
{
    switch (event) {
        case ZLINK_EVENT_CONNECTED:
            return "CONNECTED";
        case ZLINK_EVENT_CONNECT_DELAYED:
            return "CONNECT_DELAYED";
        case ZLINK_EVENT_CONNECT_RETRIED:
            return "CONNECT_RETRIED";
        case ZLINK_EVENT_LISTENING:
            return "LISTENING";
        case ZLINK_EVENT_BIND_FAILED:
            return "BIND_FAILED";
        case ZLINK_EVENT_ACCEPTED:
            return "ACCEPTED";
        case ZLINK_EVENT_ACCEPT_FAILED:
            return "ACCEPT_FAILED";
        case ZLINK_EVENT_CLOSED:
            return "CLOSED";
        case ZLINK_EVENT_CLOSE_FAILED:
            return "CLOSE_FAILED";
        case ZLINK_EVENT_DISCONNECTED:
            return "DISCONNECTED";
        case ZLINK_EVENT_MONITOR_STOPPED:
            return "MONITOR_STOPPED";
        case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            return "HANDSHAKE_FAILED_NO_DETAIL";
        case ZLINK_EVENT_CONNECTION_READY:
            return "CONNECTION_READY";
        case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            return "HANDSHAKE_FAILED_PROTOCOL";
        case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
            return "HANDSHAKE_FAILED_AUTH";
        default:
            return "UNKNOWN";
    }
}

void print_events (void *socket, int timeout, int limit)
{
    // print events received
    int value;
    char *event_address;
    int event =
      get_monitor_event_with_timeout (socket, &value, &event_address, timeout);
    int i = 0;
    ;
    while ((event != -1) && (++i < limit)) {
        const char *eventName = get_zlinkEventName (event);
        printf ("Got event: %s\n", eventName);
        event = get_monitor_event_with_timeout (socket, &value, &event_address,
                                                timeout);
    }
}
