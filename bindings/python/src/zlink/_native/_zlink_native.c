/* SPDX-License-Identifier: MPL-2.0 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlink.h>

typedef int (*submit_part_fn) (void *, zlink_msg_t *, zlink_send_flags_t,
                               zlink_part_flag_t);
typedef int (*submit_rid_part_fn) (void *, const zlink_routing_id_t *,
                                   zlink_msg_t *, zlink_send_flags_t,
                                   zlink_part_flag_t);
typedef int (*publish_part_fn) (void *, const char *, zlink_msg_t *,
                                zlink_send_flags_t, zlink_part_flag_t);

static int copy_routing_id (Py_buffer *view, zlink_routing_id_t *rid);

typedef struct prepared_parts_t
{
    zlink_msg_t *parts;
    Py_buffer *views;
    Py_ssize_t count;
} prepared_parts_t;

typedef struct received_parts_t
{
    zlink_msg_t *parts;
    Py_ssize_t count;
    Py_ssize_t capacity;
} received_parts_t;

typedef struct socket_send_op_t
{
    PyObject_HEAD
    void *handle;
    PyObject *payload;
    PyObject *parts;
    int flags;
    int submitted;
} socket_send_op_t;

typedef struct routed_send_op_t
{
    PyObject_HEAD
    void *handle;
    zlink_routing_id_t routing_id;
    PyObject *payload;
    PyObject *parts;
    int flags;
    int submitted;
} routed_send_op_t;

typedef struct publisher_send_op_t
{
    PyObject_HEAD
    void *handle;
    PyObject *topic;
    PyObject *payload;
    PyObject *parts;
    int flags;
    int submitted;
} publisher_send_op_t;

typedef struct bytes_parts_owner_t
{
    PyObject_HEAD
    PyObject *parts;
    char *open_parts;
    char inline_open_part;
    Py_ssize_t part_count;
    int closed;
} bytes_parts_owner_t;

typedef struct native_parts_owner_t
{
    PyObject_HEAD
    zlink_msg_t *parts;
    char *open_parts;
    char inline_open_part;
    Py_ssize_t part_count;
    int closed;
} native_parts_owner_t;

typedef struct latency_samples_t
{
    double *values;
    size_t count;
    size_t capacity;
    double sum;
} latency_samples_t;

static int
is_recv_no_data_result (int rc)
{
    return rc == ZLINK_RECV_NO_DATA;
}

typedef struct single_socket_bench_t
{
    void *sender;
    void *receiver;
    size_t msg_size;
    int duration_s;
    int send_mode;
    int recv_mode;
    uint32_t run_id;
    zlink_routing_id_t target_rid;
    char topic[256];
    size_t topic_len;
    uint64_t sent;
    uint64_t received;
    int ok;
    int err;
    pthread_mutex_t latency_lock;
    latency_samples_t latencies;
} single_socket_bench_t;

typedef struct stream_echo_item_t
{
    zlink_routing_id_t routing_id;
    zlink_msg_t msg;
    struct stream_echo_item_t *next;
} stream_echo_item_t;

typedef struct stream_echo_session_t
{
    void *handle;
    pthread_mutex_t send_lock;
    pthread_mutex_t queue_lock;
    stream_echo_item_t *head;
    stream_echo_item_t *tail;
    char *stop_token;
    size_t stop_token_len;
    int stop_requested;
    int failed;
    int last_errno;
    unsigned long long recv_count;
    unsigned long long send_count;
    unsigned long long pending_count;
} stream_echo_session_t;

typedef struct routed_echo_item_t
{
    zlink_routing_id_t routing_id;
    zlink_msg_t msg;
    struct routed_echo_item_t *next;
} routed_echo_item_t;

typedef struct routed_echo_queue_t
{
    routed_echo_item_t *head;
    routed_echo_item_t *tail;
    unsigned long long pending_count;
} routed_echo_queue_t;

typedef struct spot_count_session_t
{
    void *handle;
    pthread_mutex_t lock;
    uint32_t run_id;
    size_t msg_size;
    char topic[256];
    size_t topic_len;
    char *stop_token;
    size_t stop_token_len;
    int ready_seen;
    int active;
    int stopped;
    int failed;
    int last_errno;
    uint64_t active_deadline_ns;
    unsigned long long received;
    latency_samples_t latencies;
} spot_count_session_t;

typedef struct spot_routed_echo_session_t
{
    void *handle;
    int reply_mode;
    int failed;
    int last_errno;
    unsigned long long received;
    unsigned long long sent;
} spot_routed_echo_session_t;

typedef struct spot_reqrep_client_state_t spot_reqrep_client_state_t;

typedef struct spot_reqrep_client_slot_t
{
    spot_reqrep_client_state_t *owner;
    void *handle;
    int waiting;
    uint64_t seq;
    unsigned char *payload;
} spot_reqrep_client_slot_t;

struct spot_reqrep_client_state_t
{
    pthread_mutex_t lock;
    uint32_t run_id;
    size_t msg_size;
    uint64_t deadline_ns;
    unsigned long long received;
    int failed;
    int last_errno;
    latency_samples_t latencies;
};

static int copy_routing_id (Py_buffer *view, zlink_routing_id_t *rid);

static void
release_prepared_parts (prepared_parts_t *prepared, Py_ssize_t close_from)
{
    if (!prepared)
        return;
    if (prepared->parts) {
        for (Py_ssize_t i = close_from; i < prepared->count; ++i)
            zlink_msg_close (&prepared->parts[i]);
        PyMem_Free (prepared->parts);
        prepared->parts = NULL;
    }
    if (prepared->views) {
        for (Py_ssize_t i = 0; i < prepared->count; ++i) {
            if (prepared->views[i].obj)
                PyBuffer_Release (&prepared->views[i]);
        }
        PyMem_Free (prepared->views);
        prepared->views = NULL;
    }
    prepared->count = 0;
}

static int
payload_items (PyObject *payload, PyObject ***items_out, Py_ssize_t *count_out)
{
    PyObject **items = NULL;
    Py_ssize_t count = 1;

    if (PyList_Check (payload) || PyTuple_Check (payload)) {
        count = PySequence_Size (payload);
        if (count <= 0) {
            PyErr_SetString (PyExc_ValueError, "parts must not be empty");
            return -1;
        }
        items = PyMem_Calloc ((size_t) count, sizeof (PyObject *));
        if (!items) {
            PyErr_NoMemory ();
            return -1;
        }
        for (Py_ssize_t i = 0; i < count; ++i) {
            items[i] = PySequence_Fast_GET_ITEM (payload, i);
        }
    } else {
        items = PyMem_Calloc (1, sizeof (PyObject *));
        if (!items) {
            PyErr_NoMemory ();
            return -1;
        }
        items[0] = payload;
    }

    *items_out = items;
    *count_out = count;
    return 0;
}

static int
prepare_parts (PyObject *payload, prepared_parts_t *prepared)
{
    PyObject **items = NULL;
    Py_ssize_t count = 0;

    memset (prepared, 0, sizeof (*prepared));
    if (payload_items (payload, &items, &count) != 0)
        return -1;

    prepared->parts = PyMem_Calloc ((size_t) count, sizeof (zlink_msg_t));
    prepared->views = PyMem_Calloc ((size_t) count, sizeof (Py_buffer));
    prepared->count = count;
    if (!prepared->parts || !prepared->views) {
        PyMem_Free (items);
        release_prepared_parts (prepared, 0);
        PyErr_NoMemory ();
        return -1;
    }

    for (Py_ssize_t i = 0; i < count; ++i) {
        if (PyObject_GetBuffer (items[i], &prepared->views[i],
                                PyBUF_CONTIG_RO) != 0) {
            PyMem_Free (items);
            release_prepared_parts (prepared, 0);
            return -1;
        }

        if (zlink_msg_init_size (&prepared->parts[i],
                                 (size_t) prepared->views[i].len)
            != ZLINK_CONFIG_OK) {
            int err = zlink_errno ();
            PyMem_Free (items);
            release_prepared_parts (prepared, 0);
            if (err != 0)
                errno = err;
            PyErr_SetFromErrnoWithFilename (PyExc_OSError, NULL);
            return -1;
        }
        if (prepared->views[i].len > 0) {
            memcpy (zlink_msg_data (&prepared->parts[i]),
                    prepared->views[i].buf,
                    (size_t) prepared->views[i].len);
        }
    }

    PyMem_Free (items);
    return 0;
}

static zlink_part_flag_t
part_flag (Py_ssize_t index, Py_ssize_t count)
{
    return (index + 1 < count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
}

static PyObject *
result_tuple (int rc, int err)
{
    return Py_BuildValue ("ii", rc, err);
}

static void
bytes_parts_owner_dealloc (bytes_parts_owner_t *owner)
{
    Py_XDECREF (owner->parts);
    if (owner->open_parts != &owner->inline_open_part)
        PyMem_Free (owner->open_parts);
    Py_TYPE (owner)->tp_free ((PyObject *) owner);
}

static int
bytes_parts_owner_check_index (bytes_parts_owner_t *owner, Py_ssize_t index)
{
    if (index < 0 || index >= owner->part_count) {
        PyErr_SetString (PyExc_IndexError, "received part index out of range");
        return -1;
    }
    if (owner->closed || !owner->open_parts[index]) {
        PyErr_SetString (PyExc_RuntimeError, "received message is closed");
        return -1;
    }
    return 0;
}

static int
bytes_parts_owner_index (bytes_parts_owner_t *owner, PyObject *index_obj,
                         Py_ssize_t *index_out)
{
    Py_ssize_t index = PyLong_AsSsize_t (index_obj);
    if (index == -1 && PyErr_Occurred ())
        return -1;
    if (bytes_parts_owner_check_index (owner, index) != 0)
        return -1;
    *index_out = index;
    return 0;
}

static PyObject *
bytes_parts_owner_part_count (bytes_parts_owner_t *owner, void *closure)
{
    (void) closure;
    return PyLong_FromSsize_t (owner->part_count);
}

static PyObject *
bytes_parts_owner_size (bytes_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = 0;
    PyObject *part = NULL;
    if (bytes_parts_owner_index (owner, index_obj, &index) != 0)
        return NULL;
    part = PyTuple_GET_ITEM (owner->parts, index);
    return PyLong_FromSsize_t (PyBytes_GET_SIZE (part));
}

static PyObject *
bytes_parts_owner_data (bytes_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = 0;
    PyObject *part = NULL;
    if (bytes_parts_owner_index (owner, index_obj, &index) != 0)
        return NULL;
    part = PyTuple_GET_ITEM (owner->parts, index);
    return PyMemoryView_FromObject (part);
}

static PyObject *
bytes_parts_owner_to_bytes (bytes_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = 0;
    PyObject *part = NULL;
    if (bytes_parts_owner_index (owner, index_obj, &index) != 0)
        return NULL;
    part = PyTuple_GET_ITEM (owner->parts, index);
    Py_INCREF (part);
    return part;
}

static PyObject *
bytes_parts_owner_close_part (bytes_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = PyLong_AsSsize_t (index_obj);
    if (index == -1 && PyErr_Occurred ())
        return NULL;
    if (owner->closed || index < 0 || index >= owner->part_count)
        Py_RETURN_NONE;
    owner->open_parts[index] = 0;
    for (Py_ssize_t i = 0; i < owner->part_count; ++i) {
        if (owner->open_parts[i])
            Py_RETURN_NONE;
    }
    owner->closed = 1;
    Py_RETURN_NONE;
}

static PyObject *
bytes_parts_owner_close (bytes_parts_owner_t *owner, PyObject *Py_UNUSED (ignored))
{
    if (!owner->closed) {
        owner->closed = 1;
        if (owner->open_parts)
            memset (owner->open_parts, 0, (size_t) owner->part_count);
    }
    Py_RETURN_NONE;
}

static PyMethodDef bytes_parts_owner_methods[] = {
    { "size", (PyCFunction) bytes_parts_owner_size, METH_O,
      "Return part size." },
    { "data", (PyCFunction) bytes_parts_owner_data, METH_O,
      "Return part memoryview." },
    { "to_bytes", (PyCFunction) bytes_parts_owner_to_bytes, METH_O,
      "Return part bytes." },
    { "close_part", (PyCFunction) bytes_parts_owner_close_part, METH_O,
      "Close one part." },
    { "close", (PyCFunction) bytes_parts_owner_close, METH_NOARGS,
      "Close all parts." },
    { NULL, NULL, 0, NULL },
};

static PyGetSetDef bytes_parts_owner_getset[] = {
    { "_part_count", (getter) bytes_parts_owner_part_count, NULL,
      "part count", NULL },
    { NULL, NULL, NULL, NULL, NULL },
};

static PyTypeObject bytes_parts_owner_type = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "zlink._native._zlink_native.BytesReceivedPartsOwner",
    .tp_basicsize = sizeof (bytes_parts_owner_t),
    .tp_dealloc = (destructor) bytes_parts_owner_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = bytes_parts_owner_methods,
    .tp_getset = bytes_parts_owner_getset,
};

static void
native_parts_owner_close_all (native_parts_owner_t *owner)
{
    if (owner->closed)
        return;
    owner->closed = 1;
    if (owner->parts) {
        for (Py_ssize_t i = 0; i < owner->part_count; ++i) {
            if (owner->open_parts && owner->open_parts[i]) {
                zlink_msg_close (&owner->parts[i]);
                owner->open_parts[i] = 0;
            }
        }
        free (owner->parts);
        owner->parts = NULL;
    }
}

static void
native_parts_owner_dealloc (native_parts_owner_t *owner)
{
    native_parts_owner_close_all (owner);
    if (owner->open_parts != &owner->inline_open_part)
        PyMem_Free (owner->open_parts);
    Py_TYPE (owner)->tp_free ((PyObject *) owner);
}

static int
native_parts_owner_check_index (native_parts_owner_t *owner, Py_ssize_t index)
{
    if (index < 0 || index >= owner->part_count) {
        PyErr_SetString (PyExc_IndexError, "received part index out of range");
        return -1;
    }
    if (owner->closed || !owner->parts || !owner->open_parts[index]) {
        PyErr_SetString (PyExc_RuntimeError, "received message is closed");
        return -1;
    }
    return 0;
}

static int
native_parts_owner_index (native_parts_owner_t *owner, PyObject *index_obj,
                          Py_ssize_t *index_out)
{
    Py_ssize_t index = PyLong_AsSsize_t (index_obj);
    if (index == -1 && PyErr_Occurred ())
        return -1;
    if (native_parts_owner_check_index (owner, index) != 0)
        return -1;
    *index_out = index;
    return 0;
}

static PyObject *
native_parts_owner_part_count (native_parts_owner_t *owner, void *closure)
{
    (void) closure;
    return PyLong_FromSsize_t (owner->part_count);
}

static PyObject *
native_parts_owner_size (native_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = 0;
    if (native_parts_owner_index (owner, index_obj, &index) != 0)
        return NULL;
    return PyLong_FromSize_t (zlink_msg_size (&owner->parts[index]));
}

static PyObject *
native_parts_owner_data (native_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = 0;
    Py_buffer view;
    void *data = NULL;
    Py_ssize_t size = 0;

    if (native_parts_owner_index (owner, index_obj, &index) != 0)
        return NULL;
    data = zlink_msg_data (&owner->parts[index]);
    size = (Py_ssize_t) zlink_msg_size (&owner->parts[index]);
    if (!data || size <= 0) {
        PyObject *empty = PyBytes_FromStringAndSize ("", 0);
        PyObject *view_obj = NULL;
        if (!empty)
            return NULL;
        view_obj = PyMemoryView_FromObject (empty);
        Py_DECREF (empty);
        return view_obj;
    }
    if (PyBuffer_FillInfo (&view, (PyObject *) owner, data, size, 1,
                           PyBUF_CONTIG_RO)
        != 0)
        return NULL;
    return PyMemoryView_FromBuffer (&view);
}

static PyObject *
native_parts_owner_to_bytes (native_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = 0;
    void *data = NULL;
    size_t size = 0;

    if (native_parts_owner_index (owner, index_obj, &index) != 0)
        return NULL;
    data = zlink_msg_data (&owner->parts[index]);
    size = zlink_msg_size (&owner->parts[index]);
    return PyBytes_FromStringAndSize ((const char *) data, (Py_ssize_t) size);
}

static PyObject *
native_parts_owner_close_part (native_parts_owner_t *owner, PyObject *index_obj)
{
    Py_ssize_t index = PyLong_AsSsize_t (index_obj);
    int any_open = 0;
    if (index == -1 && PyErr_Occurred ())
        return NULL;
    if (owner->closed || !owner->parts || index < 0 || index >= owner->part_count)
        Py_RETURN_NONE;
    if (owner->open_parts[index]) {
        zlink_msg_close (&owner->parts[index]);
        owner->open_parts[index] = 0;
    }
    for (Py_ssize_t i = 0; i < owner->part_count; ++i) {
        if (owner->open_parts[i]) {
            any_open = 1;
            break;
        }
    }
    if (!any_open) {
        free (owner->parts);
        owner->parts = NULL;
        owner->closed = 1;
    }
    Py_RETURN_NONE;
}

static PyObject *
native_parts_owner_close (native_parts_owner_t *owner,
                          PyObject *Py_UNUSED (ignored))
{
    native_parts_owner_close_all (owner);
    Py_RETURN_NONE;
}

static PyMethodDef native_parts_owner_methods[] = {
    { "size", (PyCFunction) native_parts_owner_size, METH_O,
      "Return part size." },
    { "data", (PyCFunction) native_parts_owner_data, METH_O,
      "Return part memoryview." },
    { "to_bytes", (PyCFunction) native_parts_owner_to_bytes, METH_O,
      "Return part bytes." },
    { "close_part", (PyCFunction) native_parts_owner_close_part, METH_O,
      "Close one part." },
    { "close", (PyCFunction) native_parts_owner_close, METH_NOARGS,
      "Close all parts." },
    { NULL, NULL, 0, NULL },
};

static PyGetSetDef native_parts_owner_getset[] = {
    { "_part_count", (getter) native_parts_owner_part_count, NULL,
      "part count", NULL },
    { NULL, NULL, NULL, NULL, NULL },
};

static PyTypeObject native_parts_owner_type = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "zlink._native._zlink_native.NativeReceivedPartsOwner",
    .tp_basicsize = sizeof (native_parts_owner_t),
    .tp_dealloc = (destructor) native_parts_owner_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = native_parts_owner_methods,
    .tp_getset = native_parts_owner_getset,
};

static PyObject *
build_native_parts_owner (received_parts_t *received)
{
    native_parts_owner_t *owner = NULL;

    owner = PyObject_New (native_parts_owner_t, &native_parts_owner_type);
    if (!owner)
        return NULL;
    owner->parts = received->parts;
    owner->part_count = received->count;
    owner->closed = 0;
    owner->open_parts = NULL;
    owner->inline_open_part = 0;
    if (received->count == 1) {
        owner->inline_open_part = 1;
        owner->open_parts = &owner->inline_open_part;
    } else {
        owner->open_parts = PyMem_Calloc ((size_t) received->count, sizeof (char));
        if (!owner->open_parts) {
            owner->parts = NULL;
            Py_DECREF (owner);
            PyErr_NoMemory ();
            return NULL;
        }
        memset (owner->open_parts, 1, (size_t) received->count);
    }
    received->parts = NULL;
    received->count = 0;
    received->capacity = 0;
    return (PyObject *) owner;
}

static int
raise_submit_error (int result, int err)
{
    PyObject *module = PyImport_ImportModule ("zlink.contracts.errors.errors");
    PyObject *error_type = NULL;
    PyObject *error = NULL;

    if (!module)
        return -1;
    error_type = PyObject_GetAttrString (module, "SubmitError");
    Py_DECREF (module);
    if (!error_type)
        return -1;
    error = PyObject_CallFunction (error_type, "ii", result, err);
    Py_DECREF (error_type);
    if (!error)
        return -1;
    PyErr_SetObject ((PyObject *) Py_TYPE (error), error);
    Py_DECREF (error);
    return -1;
}

static int
socket_send_op_add_message (socket_send_op_t *op, PyObject *payload)
{
    if (op->parts) {
        return PyList_Append (op->parts, payload);
    }
    if (!op->payload) {
        Py_INCREF (payload);
        op->payload = payload;
        return 0;
    }

    PyObject *parts = PyList_New (2);
    if (!parts)
        return -1;
    PyList_SET_ITEM (parts, 0, op->payload);
    Py_INCREF (payload);
    PyList_SET_ITEM (parts, 1, payload);
    op->payload = NULL;
    op->parts = parts;
    return 0;
}

static void
socket_send_op_dealloc (socket_send_op_t *op)
{
    Py_XDECREF (op->payload);
    Py_XDECREF (op->parts);
    Py_TYPE (op)->tp_free ((PyObject *) op);
}

static PyObject *
socket_send_op_message (socket_send_op_t *op, PyObject *payload)
{
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    if (socket_send_op_add_message (op, payload) != 0)
        return NULL;
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
socket_send_op_messages (socket_send_op_t *op, PyObject *args)
{
    Py_ssize_t count = PyTuple_GET_SIZE (args);
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    for (Py_ssize_t i = 0; i < count; ++i) {
        if (socket_send_op_add_message (op, PyTuple_GET_ITEM (args, i)) != 0)
            return NULL;
    }
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
socket_send_op_flags (socket_send_op_t *op, PyObject *flags)
{
    long value = 0;
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    value = PyLong_AsLong (flags);
    if (value == -1 && PyErr_Occurred ())
        return NULL;
    op->flags = (int) value;
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
socket_send_op_submit (socket_send_op_t *op, PyObject *Py_UNUSED (ignored))
{
    PyObject *payload = NULL;
    prepared_parts_t prepared;
    Py_ssize_t close_from = 0;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;

    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    payload = op->parts ? op->parts : op->payload;
    if (!payload)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_ARGUMENT, 0), NULL;

    op->submitted = 1;
    if (!op->parts) {
        Py_buffer view = { 0 };
        const char *payload_data = NULL;
        Py_ssize_t payload_size = 0;
        int has_view = 0;
        zlink_msg_t part;

        if (PyByteArray_Check (payload)) {
            payload_data = PyByteArray_AS_STRING (payload);
            payload_size = PyByteArray_GET_SIZE (payload);
        } else if (PyBytes_Check (payload)) {
            payload_data = PyBytes_AS_STRING (payload);
            payload_size = PyBytes_GET_SIZE (payload);
        } else {
            if (PyObject_GetBuffer (payload, &view, PyBUF_CONTIG_RO) != 0)
                return NULL;
            payload_data = (const char *) view.buf;
            payload_size = view.len;
            has_view = 1;
        }
        if (zlink_msg_init_size (&part, (size_t) payload_size)
            != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (has_view)
                PyBuffer_Release (&view);
            if (err != 0)
                errno = err;
            PyErr_SetFromErrnoWithFilename (PyExc_OSError, NULL);
            return NULL;
        }
        if (payload_size > 0)
            memcpy (zlink_msg_data (&part), payload_data,
                    (size_t) payload_size);

        Py_BEGIN_ALLOW_THREADS
        rc = zlink_send_part (op->handle, &part, (zlink_send_flags_t) op->flags,
                              ZLINK_PART_FINAL);
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
        }
        Py_END_ALLOW_THREADS

        if (has_view)
            PyBuffer_Release (&view);
        if (rc == ZLINK_SUBMIT_OK)
            Py_RETURN_TRUE;
        if ((op->flags & ZLINK_DONTWAIT) && rc == ZLINK_SUBMIT_BACKPRESSURED)
            Py_RETURN_FALSE;
        raise_submit_error (rc, err);
        return NULL;
    }

    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_send_part (op->handle, &prepared.parts[i],
                              (zlink_send_flags_t) op->flags,
                              part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    if (rc == ZLINK_SUBMIT_OK)
        Py_RETURN_TRUE;
    if ((op->flags & ZLINK_DONTWAIT) && rc == ZLINK_SUBMIT_BACKPRESSURED)
        Py_RETURN_FALSE;
    raise_submit_error (rc, err);
    return NULL;
}

static PyMethodDef socket_send_op_methods[] = {
    { "message", (PyCFunction) socket_send_op_message, METH_O,
      "Add one payload part." },
    { "messages", (PyCFunction) socket_send_op_messages, METH_VARARGS,
      "Add payload parts." },
    { "flags", (PyCFunction) socket_send_op_flags, METH_O,
      "Set send flags." },
    { "submit", (PyCFunction) socket_send_op_submit, METH_NOARGS,
      "Submit payload parts." },
    { NULL, NULL, 0, NULL },
};

static PyTypeObject socket_send_op_type = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "zlink._native._zlink_native.SocketSendOp",
    .tp_basicsize = sizeof (socket_send_op_t),
    .tp_dealloc = (destructor) socket_send_op_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = socket_send_op_methods,
};

static PyObject *
py_socket_send_op (PyObject *self, PyObject *handle_obj)
{
    unsigned long long handle_value = 0;
    socket_send_op_t *op = NULL;

    (void) self;
    handle_value = PyLong_AsUnsignedLongLong (handle_obj);
    if (handle_value == (unsigned long long) -1 && PyErr_Occurred ())
        return NULL;
    op = PyObject_New (socket_send_op_t, &socket_send_op_type);
    if (!op)
        return NULL;
    op->handle = (void *) (uintptr_t) handle_value;
    op->payload = NULL;
    op->parts = NULL;
    op->flags = 0;
    op->submitted = 0;
    return (PyObject *) op;
}

static int
routed_send_op_add_message (routed_send_op_t *op, PyObject *payload)
{
    if (op->parts)
        return PyList_Append (op->parts, payload);
    if (!op->payload) {
        Py_INCREF (payload);
        op->payload = payload;
        return 0;
    }

    PyObject *parts = PyList_New (2);
    if (!parts)
        return -1;
    PyList_SET_ITEM (parts, 0, op->payload);
    Py_INCREF (payload);
    PyList_SET_ITEM (parts, 1, payload);
    op->payload = NULL;
    op->parts = parts;
    return 0;
}

static void
routed_send_op_dealloc (routed_send_op_t *op)
{
    Py_XDECREF (op->payload);
    Py_XDECREF (op->parts);
    Py_TYPE (op)->tp_free ((PyObject *) op);
}

static PyObject *
routed_send_op_message (routed_send_op_t *op, PyObject *payload)
{
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    if (routed_send_op_add_message (op, payload) != 0)
        return NULL;
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
routed_send_op_messages (routed_send_op_t *op, PyObject *args)
{
    Py_ssize_t count = PyTuple_GET_SIZE (args);
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    for (Py_ssize_t i = 0; i < count; ++i) {
        if (routed_send_op_add_message (op, PyTuple_GET_ITEM (args, i)) != 0)
            return NULL;
    }
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
routed_send_op_flags (routed_send_op_t *op, PyObject *flags)
{
    long value = 0;
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    value = PyLong_AsLong (flags);
    if (value == -1 && PyErr_Occurred ())
        return NULL;
    op->flags = (int) value;
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
routed_send_op_submit (routed_send_op_t *op, PyObject *Py_UNUSED (ignored))
{
    PyObject *payload = NULL;
    prepared_parts_t prepared;
    Py_ssize_t close_from = 0;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;

    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    payload = op->parts ? op->parts : op->payload;
    if (!payload)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_ARGUMENT, 0), NULL;

    op->submitted = 1;
    if (!op->parts) {
        Py_buffer view = { 0 };
        const char *payload_data = NULL;
        Py_ssize_t payload_size = 0;
        int has_view = 0;
        zlink_msg_t part;

        if (PyByteArray_Check (payload)) {
            payload_data = PyByteArray_AS_STRING (payload);
            payload_size = PyByteArray_GET_SIZE (payload);
        } else if (PyBytes_Check (payload)) {
            payload_data = PyBytes_AS_STRING (payload);
            payload_size = PyBytes_GET_SIZE (payload);
        } else {
            if (PyObject_GetBuffer (payload, &view, PyBUF_CONTIG_RO) != 0)
                return NULL;
            payload_data = (const char *) view.buf;
            payload_size = view.len;
            has_view = 1;
        }
        if (zlink_msg_init_size (&part, (size_t) payload_size)
            != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (has_view)
                PyBuffer_Release (&view);
            if (err != 0)
                errno = err;
            PyErr_SetFromErrnoWithFilename (PyExc_OSError, NULL);
            return NULL;
        }
        if (payload_size > 0)
            memcpy (zlink_msg_data (&part), payload_data,
                    (size_t) payload_size);

        Py_BEGIN_ALLOW_THREADS
        rc = zlink_send_part_rid (op->handle, &op->routing_id, &part,
                                  (zlink_send_flags_t) op->flags,
                                  ZLINK_PART_FINAL);
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
        }
        Py_END_ALLOW_THREADS

        if (has_view)
            PyBuffer_Release (&view);
        if (rc == ZLINK_SUBMIT_OK)
            Py_RETURN_TRUE;
        if ((op->flags & ZLINK_DONTWAIT) && rc == ZLINK_SUBMIT_BACKPRESSURED)
            Py_RETURN_FALSE;
        raise_submit_error (rc, err);
        return NULL;
    }

    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_send_part_rid (op->handle, &op->routing_id,
                                  &prepared.parts[i],
                                  (zlink_send_flags_t) op->flags,
                                  part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    if (rc == ZLINK_SUBMIT_OK)
        Py_RETURN_TRUE;
    if ((op->flags & ZLINK_DONTWAIT) && rc == ZLINK_SUBMIT_BACKPRESSURED)
        Py_RETURN_FALSE;
    raise_submit_error (rc, err);
    return NULL;
}

static PyMethodDef routed_send_op_methods[] = {
    { "message", (PyCFunction) routed_send_op_message, METH_O,
      "Add one payload part." },
    { "messages", (PyCFunction) routed_send_op_messages, METH_VARARGS,
      "Add payload parts." },
    { "flags", (PyCFunction) routed_send_op_flags, METH_O,
      "Set send flags." },
    { "submit", (PyCFunction) routed_send_op_submit, METH_NOARGS,
      "Submit routed payload parts." },
    { NULL, NULL, 0, NULL },
};

static PyTypeObject routed_send_op_type = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "zlink._native._zlink_native.RoutedSendOp",
    .tp_basicsize = sizeof (routed_send_op_t),
    .tp_dealloc = (destructor) routed_send_op_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = routed_send_op_methods,
};

static PyObject *
py_routed_send_op (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    PyObject *routing_id_obj = NULL;
    Py_buffer rid_view = { 0 };
    routed_send_op_t *op = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "KO", &handle_value, &routing_id_obj))
        return NULL;
    op = PyObject_New (routed_send_op_t, &routed_send_op_type);
    if (!op)
        return NULL;
    op->handle = (void *) (uintptr_t) handle_value;
    op->payload = NULL;
    op->parts = NULL;
    op->flags = 0;
    op->submitted = 0;
    if (PyObject_GetBuffer (routing_id_obj, &rid_view, PyBUF_CONTIG_RO) != 0) {
        Py_DECREF (op);
        return NULL;
    }
    if (copy_routing_id (&rid_view, &op->routing_id) != 0) {
        PyBuffer_Release (&rid_view);
        Py_DECREF (op);
        return NULL;
    }
    PyBuffer_Release (&rid_view);
    return (PyObject *) op;
}

static PyObject *
validated_topic_bytes (PyObject *topic)
{
    PyObject *topic_bytes = NULL;
    Py_buffer view = { 0 };

    if (PyUnicode_Check (topic)) {
        topic_bytes = PyUnicode_AsUTF8String (topic);
        if (!topic_bytes)
            return NULL;
    } else if (PyBytes_Check (topic)) {
        Py_INCREF (topic);
        topic_bytes = topic;
    } else {
        if (PyObject_GetBuffer (topic, &view, PyBUF_CONTIG_RO) != 0)
            return NULL;
        topic_bytes =
          PyBytes_FromStringAndSize ((const char *) view.buf, view.len);
        PyBuffer_Release (&view);
        if (!topic_bytes)
            return NULL;
    }

    if (memchr (PyBytes_AS_STRING (topic_bytes), '\0',
                (size_t) PyBytes_GET_SIZE (topic_bytes))) {
        Py_DECREF (topic_bytes);
        PyErr_SetString (PyExc_ValueError,
                         "topic must not contain NUL bytes");
        return NULL;
    }
    return topic_bytes;
}

static int
publisher_send_op_add_message (publisher_send_op_t *op, PyObject *payload)
{
    if (op->parts)
        return PyList_Append (op->parts, payload);
    if (!op->payload) {
        Py_INCREF (payload);
        op->payload = payload;
        return 0;
    }

    PyObject *parts = PyList_New (2);
    if (!parts)
        return -1;
    PyList_SET_ITEM (parts, 0, op->payload);
    Py_INCREF (payload);
    PyList_SET_ITEM (parts, 1, payload);
    op->payload = NULL;
    op->parts = parts;
    return 0;
}

static void
publisher_send_op_dealloc (publisher_send_op_t *op)
{
    Py_XDECREF (op->topic);
    Py_XDECREF (op->payload);
    Py_XDECREF (op->parts);
    Py_TYPE (op)->tp_free ((PyObject *) op);
}

static PyObject *
publisher_send_op_message (publisher_send_op_t *op, PyObject *payload)
{
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    if (publisher_send_op_add_message (op, payload) != 0)
        return NULL;
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
publisher_send_op_messages (publisher_send_op_t *op, PyObject *args)
{
    Py_ssize_t count = PyTuple_GET_SIZE (args);
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    for (Py_ssize_t i = 0; i < count; ++i) {
        if (publisher_send_op_add_message (op, PyTuple_GET_ITEM (args, i)) != 0)
            return NULL;
    }
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
publisher_send_op_flags (publisher_send_op_t *op, PyObject *flags)
{
    long value = 0;
    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    value = PyLong_AsLong (flags);
    if (value == -1 && PyErr_Occurred ())
        return NULL;
    op->flags = (int) value;
    Py_INCREF (op);
    return (PyObject *) op;
}

static PyObject *
publisher_send_op_submit (publisher_send_op_t *op,
                          PyObject *Py_UNUSED (ignored))
{
    PyObject *payload = NULL;
    prepared_parts_t prepared;
    Py_ssize_t close_from = 0;
    const char *topic = NULL;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;

    if (op->submitted)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_STATE, 0), NULL;
    payload = op->parts ? op->parts : op->payload;
    if (!payload)
        return raise_submit_error (ZLINK_SUBMIT_INVALID_ARGUMENT, 0), NULL;

    if (!op->parts) {
        Py_buffer view = { 0 };
        const char *payload_data = NULL;
        Py_ssize_t payload_size = 0;
        int has_view = 0;
        zlink_msg_t part;

        op->submitted = 1;
        if (PyByteArray_Check (payload)) {
            payload_data = PyByteArray_AS_STRING (payload);
            payload_size = PyByteArray_GET_SIZE (payload);
        } else if (PyBytes_Check (payload)) {
            payload_data = PyBytes_AS_STRING (payload);
            payload_size = PyBytes_GET_SIZE (payload);
        } else {
            if (PyObject_GetBuffer (payload, &view, PyBUF_CONTIG_RO) != 0)
                return NULL;
            payload_data = (const char *) view.buf;
            payload_size = view.len;
            has_view = 1;
        }
        if (zlink_msg_init_size (&part, (size_t) payload_size)
            != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (has_view)
                PyBuffer_Release (&view);
            if (err != 0)
                errno = err;
            PyErr_SetFromErrnoWithFilename (PyExc_OSError, NULL);
            return NULL;
        }
        if (payload_size > 0)
            memcpy (zlink_msg_data (&part), payload_data,
                    (size_t) payload_size);

        topic = PyBytes_AS_STRING (op->topic);
        const int release_gil = (op->flags & ZLINK_DONTWAIT) == 0;
        PyThreadState *_save = NULL;
        if (release_gil)
            _save = PyEval_SaveThread ();
        rc = zlink_publish_part (op->handle, topic, &part,
                                 (zlink_send_flags_t) op->flags,
                                 ZLINK_PART_FINAL);
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
        }
        if (release_gil)
            PyEval_RestoreThread (_save);

        if (has_view)
            PyBuffer_Release (&view);
        if (rc == ZLINK_SUBMIT_OK)
            Py_RETURN_TRUE;
        if ((op->flags & ZLINK_DONTWAIT) && rc == ZLINK_SUBMIT_BACKPRESSURED)
            Py_RETURN_FALSE;
        raise_submit_error (rc, err);
        return NULL;
    }

    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;
    topic = PyBytes_AS_STRING (op->topic);
    op->submitted = 1;

    const int release_gil = (op->flags & ZLINK_DONTWAIT) == 0;
    PyThreadState *_save = NULL;
    if (release_gil)
        _save = PyEval_SaveThread ();
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_publish_part (op->handle, topic, &prepared.parts[i],
                                 (zlink_send_flags_t) op->flags,
                                 part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    if (release_gil)
        PyEval_RestoreThread (_save);

    release_prepared_parts (&prepared, close_from);
    if (rc == ZLINK_SUBMIT_OK)
        Py_RETURN_TRUE;
    if ((op->flags & ZLINK_DONTWAIT) && rc == ZLINK_SUBMIT_BACKPRESSURED)
        Py_RETURN_FALSE;
    raise_submit_error (rc, err);
    return NULL;
}

static PyMethodDef publisher_send_op_methods[] = {
    { "message", (PyCFunction) publisher_send_op_message, METH_O,
      "Add one payload part." },
    { "messages", (PyCFunction) publisher_send_op_messages, METH_VARARGS,
      "Add payload parts." },
    { "flags", (PyCFunction) publisher_send_op_flags, METH_O,
      "Set send flags." },
    { "submit", (PyCFunction) publisher_send_op_submit, METH_NOARGS,
      "Submit topic payload parts." },
    { NULL, NULL, 0, NULL },
};

static PyTypeObject publisher_send_op_type = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "zlink._native._zlink_native.PublisherSendOp",
    .tp_basicsize = sizeof (publisher_send_op_t),
    .tp_dealloc = (destructor) publisher_send_op_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = publisher_send_op_methods,
};

static PyObject *
py_publisher_send_op (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    PyObject *topic = NULL;
    publisher_send_op_t *op = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "KO", &handle_value, &topic))
        return NULL;
    op = PyObject_New (publisher_send_op_t, &publisher_send_op_type);
    if (!op)
        return NULL;
    op->handle = (void *) (uintptr_t) handle_value;
    op->topic = NULL;
    op->payload = NULL;
    op->parts = NULL;
    op->flags = 0;
    op->submitted = 0;
    op->topic = validated_topic_bytes (topic);
    if (!op->topic) {
        Py_DECREF (op);
        return NULL;
    }
    return (PyObject *) op;
}

static uint64_t
now_ns (void)
{
    struct timespec ts;
    clock_gettime (CLOCK_REALTIME, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL) + (uint64_t) ts.tv_nsec;
}

static void
write_le32 (unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char) (value & 0xffu);
    dst[1] = (unsigned char) ((value >> 8) & 0xffu);
    dst[2] = (unsigned char) ((value >> 16) & 0xffu);
    dst[3] = (unsigned char) ((value >> 24) & 0xffu);
}

static void
write_le64 (unsigned char *dst, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        dst[i] = (unsigned char) ((value >> (i * 8)) & 0xffu);
}

static uint32_t
read_le32 (const unsigned char *src)
{
    return ((uint32_t) src[0]) | ((uint32_t) src[1] << 8)
           | ((uint32_t) src[2] << 16) | ((uint32_t) src[3] << 24);
}

static uint64_t
read_le64 (const unsigned char *src)
{
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | (uint64_t) src[i];
    return value;
}

static PyObject *
py_perf_stamp_payload (PyObject *self, PyObject *args)
{
    static uint64_t seq = 0;
    PyObject *payload = NULL;
    Py_buffer view = { 0 };
    int phase = 0;
    unsigned int run_id = 0;
    long long requested_seq = -1;
    uint64_t header_seq = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "OiIL", &payload, &phase, &run_id,
                           &requested_seq))
        return NULL;
    if (PyObject_GetBuffer (payload, &view, PyBUF_WRITABLE) != 0)
        return NULL;
    if (view.len < 29) {
        PyBuffer_Release (&view);
        PyErr_SetString (PyExc_ValueError, "payload is too small for perf header");
        return NULL;
    }

    header_seq = requested_seq < 0 ? seq++ : (uint64_t) requested_seq;
    unsigned char *dst = (unsigned char *) view.buf;
    write_le32 (dst, 0x5A4C4E4Bu);
    write_le32 (dst + 4, (uint32_t) run_id);
    dst[8] = (unsigned char) phase;
    write_le32 (dst + 9, (uint32_t) view.len);
    write_le64 (dst + 13, header_seq);
    write_le64 (dst + 21, now_ns ());
    PyBuffer_Release (&view);
    Py_INCREF (payload);
    return payload;
}

static PyObject *
py_perf_active_latency_ns (PyObject *self, PyObject *args)
{
    static const char stop_token[] = "__zlink_perf_stop__";
    PyObject *data_obj = NULL;
    Py_buffer data = { 0 };
    int msg_size = 0;
    unsigned int run_id = 0;
    const unsigned char *src = NULL;
    uint32_t magic = 0;
    uint32_t header_run_id = 0;
    uint8_t phase = 0;
    uint32_t header_msg_size = 0;
    int64_t sent_ts_ns = 0;
    uint64_t now = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "OiI", &data_obj, &msg_size, &run_id))
        return NULL;
    if (PyObject_GetBuffer (data_obj, &data, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (data.len == (Py_ssize_t) (sizeof (stop_token) - 1)
        && memcmp (data.buf, stop_token, sizeof (stop_token) - 1) == 0) {
        PyBuffer_Release (&data);
        return PyFloat_FromDouble (-1.0);
    }
    if (data.len != msg_size || data.len < 29) {
        PyBuffer_Release (&data);
        return PyFloat_FromDouble (-2.0);
    }
    src = (const unsigned char *) data.buf;
    magic = read_le32 (src);
    header_run_id = read_le32 (src + 4);
    phase = src[8];
    header_msg_size = read_le32 (src + 9);
    sent_ts_ns = (int64_t) read_le64 (src + 21);
    PyBuffer_Release (&data);

    if (magic != 0x5A4C4E4Bu || phase != 1 || header_msg_size != (uint32_t) msg_size
        || header_run_id != run_id)
        return PyFloat_FromDouble (-2.0);
    now = now_ns ();
    if (sent_ts_ns > 0 && now >= (uint64_t) sent_ts_ns)
        return PyFloat_FromDouble ((double) (now - (uint64_t) sent_ts_ns));
    return PyFloat_FromDouble (0.0);
}

static uint64_t
steady_ns (void)
{
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL) + (uint64_t) ts.tv_nsec;
}

static void
write_u32_le (unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char) (value & 0xffU);
    dst[1] = (unsigned char) ((value >> 8) & 0xffU);
    dst[2] = (unsigned char) ((value >> 16) & 0xffU);
    dst[3] = (unsigned char) ((value >> 24) & 0xffU);
}

static void
write_u64_le (unsigned char *dst, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i)
        dst[i] = (unsigned char) ((value >> (i * 8)) & 0xffU);
}

static uint32_t
read_u32_le (const unsigned char *src)
{
    return (uint32_t) src[0] | ((uint32_t) src[1] << 8)
           | ((uint32_t) src[2] << 16) | ((uint32_t) src[3] << 24);
}

static uint64_t
read_u64_le (const unsigned char *src)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value |= ((uint64_t) src[i]) << (i * 8);
    return value;
}

static int
stamp_single_payload (unsigned char *payload, size_t size, uint32_t run_id,
                      uint8_t phase, uint64_t seq)
{
    if (!payload || size < 29)
        return 0;
    write_u32_le (payload + 0, 0x5A4C4E4BU);
    write_u32_le (payload + 4, run_id);
    payload[8] = phase;
    write_u32_le (payload + 9, (uint32_t) size);
    write_u64_le (payload + 13, seq);
    write_u64_le (payload + 21, now_ns ());
    return 1;
}

static int
stamp_active_payload (unsigned char *payload, size_t size, uint32_t run_id,
                      uint64_t seq)
{
    return stamp_single_payload (payload, size, run_id, 1, seq);
}

static int
payload_is_stop_token (const void *data, size_t size)
{
    static const char stop_token[] = "__zlink_perf_stop__";
    return data && size == sizeof (stop_token) - 1
           && memcmp (data, stop_token, sizeof (stop_token) - 1) == 0;
}

static int
decode_single_payload (const void *data, size_t size, uint32_t run_id,
                       uint64_t *sent_ts_out)
{
    const unsigned char *p = (const unsigned char *) data;
    if (!p || size < 29 || read_u32_le (p + 0) != 0x5A4C4E4BU
        || read_u32_le (p + 4) != run_id || p[8] != 1
        || read_u32_le (p + 9) != (uint32_t) size)
        return 0;
    *sent_ts_out = read_u64_le (p + 21);
    return 1;
}

static int
latency_samples_add (latency_samples_t *samples, double value)
{
    if (samples->count == samples->capacity) {
        size_t next_capacity = samples->capacity == 0 ? 1024 : samples->capacity * 2;
        double *next = (double *) realloc (samples->values,
                                           next_capacity * sizeof (double));
        if (!next)
            return 0;
        samples->values = next;
        samples->capacity = next_capacity;
    }
    samples->values[samples->count++] = value >= 0.0 ? value : 0.0;
    samples->sum += samples->values[samples->count - 1];
    return 1;
}

static int
compare_double (const void *a, const void *b)
{
    const double da = *(const double *) a;
    const double db = *(const double *) b;
    return (da > db) - (da < db);
}

static double
latency_percentile_sorted (const latency_samples_t *samples, double ratio)
{
    if (!samples || samples->count == 0)
        return 0.0;
    size_t index =
      ratio >= 0.99 ? (samples->count * 99 + 99) / 100
                    : (samples->count * 95 + 99) / 100;
    if (index == 0)
        index = 1;
    if (index > samples->count)
        index = samples->count;
    return samples->values[index - 1];
}

static int
send_owned_part_mode (single_socket_bench_t *bench, zlink_msg_t *part, int flags)
{
    int rc = ZLINK_SUBMIT_INTERNAL_ERROR;
    if (bench->send_mode == 1) {
        rc = zlink_send_part_rid (bench->sender, &bench->target_rid, part,
                                  (zlink_send_flags_t) flags, ZLINK_PART_FINAL);
    } else if (bench->send_mode == 2) {
        rc = zlink_publish_part (bench->sender, bench->topic, part,
                                 (zlink_send_flags_t) flags, ZLINK_PART_FINAL);
    } else {
        rc = zlink_send_part (bench->sender, part, (zlink_send_flags_t) flags,
                              ZLINK_PART_FINAL);
    }
    if (rc == ZLINK_SUBMIT_OK)
        return 0;
    const int err = zlink_errno ();
    zlink_msg_close (part);
    if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT
        || err == EHOSTUNREACH || err == ENOTCONN)
        return 1;
    return -1;
}

static int
send_stop_token_socket (single_socket_bench_t *bench)
{
    static const char stop_token[] = "__zlink_perf_stop__";
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, sizeof (stop_token) - 1) != 0)
        return -1;
    memcpy (zlink_msg_data (&part), stop_token, sizeof (stop_token) - 1);
    return send_owned_part_mode (bench, &part, ZLINK_SEND_FLAGS_NONE);
}

static int
recv_part_mode (single_socket_bench_t *bench, zlink_msg_t *part,
                zlink_part_flag_t *has_more, int flags)
{
    if (bench->recv_mode == 1) {
        const zlink_routing_id_t *source_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        int rc = zlink_router_recv_part (
          bench->receiver, &source_rid, &source_spot_rid, &request_seq, part,
          has_more, (zlink_recv_flags_t) flags);
        if (rc != ZLINK_RECV_OK)
            return rc;
        if (!source_rid || source_rid->size == 0 || !source_spot_rid
            || source_spot_rid->size != 0 || request_seq != 0)
            return ZLINK_RECV_INTERNAL_ERROR;
        return rc;
    }
    if (bench->recv_mode == 2) {
        const zlink_routing_id_t *source_rid = NULL;
        char topic[256];
        size_t topic_len = 0;
        int rc = zlink_subscribe_part (
          bench->receiver, &source_rid, topic, sizeof (topic), &topic_len, part,
          has_more, (zlink_recv_flags_t) flags);
        if (rc != ZLINK_RECV_OK)
            return rc;
        if (source_rid || topic_len != bench->topic_len
            || memcmp (topic, bench->topic, topic_len) != 0)
            return ZLINK_RECV_INTERNAL_ERROR;
        return rc;
    }
    {
        const zlink_routing_id_t *source_rid = NULL;
        int rc = zlink_recv_part (bench->receiver, &source_rid, part, has_more,
                                  (zlink_recv_flags_t) flags);
        if (rc == ZLINK_RECV_OK && source_rid)
            return ZLINK_RECV_INTERNAL_ERROR;
        return rc;
    }
}

static void *
single_socket_sender_thread (void *arg)
{
    single_socket_bench_t *bench = (single_socket_bench_t *) arg;
    const uint64_t deadline = steady_ns () + ((uint64_t) bench->duration_s * 1000000000ULL);
    uint64_t seq = 1;

    while (steady_ns () < deadline) {
        zlink_msg_t part;
        if (zlink_msg_init_size (&part, bench->msg_size) != 0
            || !stamp_active_payload ((unsigned char *) zlink_msg_data (&part),
                                      bench->msg_size, bench->run_id, seq)) {
            bench->ok = 0;
            bench->err = zlink_errno ();
            return NULL;
        }
        int step =
          send_owned_part_mode (bench, &part, ZLINK_SEND_FLAGS_NONE);
        if (step < 0) {
            bench->ok = 0;
            bench->err = zlink_errno ();
            return NULL;
        }
        if (step > 0)
            continue;
        ++bench->sent;
        ++seq;
    }

    for (int retry = 0; retry < 100; ++retry) {
        int step = send_stop_token_socket (bench);
        if (step == 0)
            return NULL;
        if (step < 0)
            break;
        struct timespec ts = { 0, 1000000L };
        nanosleep (&ts, NULL);
    }
    bench->ok = 0;
    bench->err = zlink_errno ();
    return NULL;
}

static void *
single_socket_receiver_thread (void *arg)
{
    single_socket_bench_t *bench = (single_socket_bench_t *) arg;
    const uint64_t deadline = steady_ns () + ((uint64_t) bench->duration_s * 1000000000ULL);

    while (bench->ok) {
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        if (zlink_msg_init (&part) != 0) {
            bench->ok = 0;
            bench->err = zlink_errno ();
            return NULL;
        }
        const int rc = recv_part_mode (bench, &part, &has_more,
                                       ZLINK_RECV_FLAGS_NONE);
        if (rc != ZLINK_RECV_OK) {
            const int err = zlink_errno ();
            zlink_msg_close (&part);
            if (err == EAGAIN || err == EINTR)
                continue;
            bench->ok = 0;
            bench->err = err;
            return NULL;
        }

        void *data = zlink_msg_data (&part);
        size_t size = zlink_msg_size (&part);
        if (payload_is_stop_token (data, size)) {
            zlink_msg_close (&part);
            return NULL;
        }
        if (has_more == ZLINK_PART_FINAL && size == bench->msg_size) {
            uint64_t sent_ts = 0;
            if (decode_single_payload (data, size, bench->run_id, &sent_ts)
                && steady_ns () < deadline) {
                uint64_t recv_ns = now_ns ();
                double sample = recv_ns >= sent_ts ? (double) (recv_ns - sent_ts) : 0.0;
                pthread_mutex_lock (&bench->latency_lock);
                if (!latency_samples_add (&bench->latencies, sample)) {
                    pthread_mutex_unlock (&bench->latency_lock);
                    zlink_msg_close (&part);
                    bench->ok = 0;
                    bench->err = ENOMEM;
                    return NULL;
                }
                pthread_mutex_unlock (&bench->latency_lock);
                ++bench->received;
            }
        }
        zlink_msg_close (&part);

        for (;;) {
            has_more = ZLINK_PART_FINAL;
            if (zlink_msg_init (&part) != 0) {
                bench->ok = 0;
                bench->err = zlink_errno ();
                return NULL;
            }
            const int burst_rc = recv_part_mode (bench, &part, &has_more,
                                                 ZLINK_DONTWAIT);
            if (burst_rc != ZLINK_RECV_OK) {
                const int err = zlink_errno ();
                zlink_msg_close (&part);
                if (err == EAGAIN || err == EINTR)
                    break;
                bench->ok = 0;
                bench->err = err;
                return NULL;
            }
            data = zlink_msg_data (&part);
            size = zlink_msg_size (&part);
            if (payload_is_stop_token (data, size)) {
                zlink_msg_close (&part);
                return NULL;
            }
            if (has_more == ZLINK_PART_FINAL && size == bench->msg_size) {
                uint64_t sent_ts = 0;
                if (decode_single_payload (data, size, bench->run_id, &sent_ts)
                    && steady_ns () < deadline) {
                    uint64_t recv_ns = now_ns ();
                    double sample =
                      recv_ns >= sent_ts ? (double) (recv_ns - sent_ts) : 0.0;
                    pthread_mutex_lock (&bench->latency_lock);
                    if (!latency_samples_add (&bench->latencies, sample)) {
                        pthread_mutex_unlock (&bench->latency_lock);
                        zlink_msg_close (&part);
                        bench->ok = 0;
                        bench->err = ENOMEM;
                        return NULL;
                    }
                    pthread_mutex_unlock (&bench->latency_lock);
                    ++bench->received;
                }
            }
            zlink_msg_close (&part);
        }
    }
    return NULL;
}

static PyObject *
py_single_socket_one_way (PyObject *self, PyObject *args)
{
    unsigned long long sender_value = 0;
    unsigned long long receiver_value = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    int send_mode = 0;
    int recv_mode = 0;
    Py_buffer aux = { 0 };
    pthread_t sender_thread;
    pthread_t receiver_thread;
    int sender_started = 0;
    int receiver_started = 0;
    single_socket_bench_t bench;

    (void) self;
    if (!PyArg_ParseTuple (args, "KKniK|iiy*", &sender_value, &receiver_value,
                           &msg_size, &duration_s, &run_id_value, &send_mode,
                           &recv_mode, &aux))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        if (aux.obj)
            PyBuffer_Release (&aux);
        PyErr_SetString (PyExc_ValueError,
                         "msg_size must be >= 29 and duration_s must be > 0");
        return NULL;
    }
    if (send_mode < 0 || send_mode > 2 || recv_mode < 0 || recv_mode > 2) {
        if (aux.obj)
            PyBuffer_Release (&aux);
        PyErr_SetString (PyExc_ValueError, "invalid single benchmark mode");
        return NULL;
    }
    if ((send_mode == 1 || send_mode == 2 || recv_mode == 2) && !aux.obj) {
        PyErr_SetString (PyExc_ValueError,
                         "routed and pubsub single benchmark modes require aux bytes");
        return NULL;
    }

    memset (&bench, 0, sizeof (bench));
    bench.sender = (void *) (uintptr_t) sender_value;
    bench.receiver = (void *) (uintptr_t) receiver_value;
    bench.msg_size = (size_t) msg_size;
    bench.duration_s = duration_s;
    bench.send_mode = send_mode;
    bench.recv_mode = recv_mode;
    bench.run_id = (uint32_t) run_id_value;
    bench.ok = 1;
    if (send_mode == 1) {
        if (aux.len <= 0 || aux.len > 255) {
            PyBuffer_Release (&aux);
            PyErr_SetString (PyExc_ValueError,
                             "routing_id length must be between 1 and 255");
            return NULL;
        }
        bench.target_rid.size = (uint8_t) aux.len;
        memcpy (bench.target_rid.data, aux.buf, (size_t) aux.len);
    }
    if (send_mode == 2 || recv_mode == 2) {
        if (aux.len <= 0 || aux.len >= (Py_ssize_t) sizeof (bench.topic)
            || memchr (aux.buf, '\0', (size_t) aux.len)) {
            PyBuffer_Release (&aux);
            PyErr_SetString (PyExc_ValueError,
                             "topic must be a non-empty C string");
            return NULL;
        }
        memcpy (bench.topic, aux.buf, (size_t) aux.len);
        bench.topic[(size_t) aux.len] = '\0';
        bench.topic_len = (size_t) aux.len;
    }
    if (aux.obj)
        PyBuffer_Release (&aux);
    pthread_mutex_init (&bench.latency_lock, NULL);

    Py_BEGIN_ALLOW_THREADS
    if (pthread_create (&receiver_thread, NULL, single_socket_receiver_thread,
                        &bench)
        == 0) {
        receiver_started = 1;
        if (pthread_create (&sender_thread, NULL, single_socket_sender_thread,
                            &bench)
            == 0) {
            sender_started = 1;
        } else {
            bench.ok = 0;
            bench.err = errno;
        }
    } else {
        bench.ok = 0;
        bench.err = errno;
    }
    if (sender_started)
        pthread_join (sender_thread, NULL);
    if (receiver_started)
        pthread_join (receiver_thread, NULL);
    Py_END_ALLOW_THREADS

    pthread_mutex_destroy (&bench.latency_lock);
    if (!bench.ok || bench.received == 0) {
        int err = bench.err;
        free (bench.latencies.values);
        return Py_BuildValue ("Kdddddi", (unsigned long long) bench.received,
                              0.0, 0.0, 0.0, 0.0, 0.0, err);
    }

    qsort (bench.latencies.values, bench.latencies.count, sizeof (double),
           compare_double);
    double mean_ms = (bench.latencies.sum / (double) bench.latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&bench.latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&bench.latencies, 0.99) / 1000000.0;
    double throughput = (double) bench.received / (double) duration_s;
    double bandwidth = ((double) bench.received * (double) bench.msg_size)
                       / (double) duration_s / 1000000.0;
    free (bench.latencies.values);
    return Py_BuildValue ("Kdddddi", (unsigned long long) bench.received,
                          throughput, bandwidth, mean_ms, p95_ms, p99_ms, 0);
}

static int
send_payload_one_part (void *handle, const void *data, size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_send_part (handle, &part, (zlink_send_flags_t) flags,
                         ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
            || err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH
            || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static int
send_payload_one_part_rid (void *handle, const zlink_routing_id_t *rid,
                           const void *data, size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_send_part_rid (handle, rid, &part, (zlink_send_flags_t) flags,
                             ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
            || err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH
            || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static PyObject *
py_multi_send_one_way (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    void **handles = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    unsigned char *payload = NULL;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "OniK", &handles_obj, &msg_size, &duration_s,
                           &run_id_value))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyErr_SetString (PyExc_ValueError, "invalid multi send arguments");
        return NULL;
    }
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq)
        return NULL;
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    payload = (unsigned char *) malloc ((size_t) msg_size);
    if (!handles || !payload) {
        free (handles);
        free (payload);
        Py_DECREF (handles_seq);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            free (payload);
            Py_DECREF (handles_seq);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
    }
    memset (payload, 'm', (size_t) msg_size);

    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    uint64_t seq = 1;

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline) {
        int progressed = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if (steady_ns () >= deadline)
                break;
            if (!stamp_single_payload (payload, (size_t) msg_size,
                                       (uint32_t) run_id_value, 1, seq)) {
                ok = 0;
                err = EINVAL;
                break;
            }
            int step = send_payload_one_part (handles[i], payload,
                                              (size_t) msg_size, ZLINK_DONTWAIT);
            if (step < 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            if (step == 0) {
                progressed = 1;
                ++sent;
                ++seq;
            }
        }
        if (!ok)
            break;
        if (!progressed) {
            struct timespec ts = { 0, 100000L };
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

    free (handles);
    free (payload);
    Py_DECREF (handles_seq);
    return Py_BuildValue ("Ki", sent, ok ? 0 : err);
}

static int
recv_echo_one_part (void *handle, int router_recv, uint32_t run_id,
                    size_t msg_size, double latency_divisor,
                    latency_samples_t *latencies, unsigned long long *received)
{
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    int rc = ZLINK_RECV_OK;
    uint64_t sent_ts = 0;

    if (zlink_msg_init (&part) != 0)
        return -1;
    if (router_recv) {
        const zlink_routing_id_t *peer_rid = NULL;
        const zlink_routing_id_t *spot_rid = NULL;
        uint64_t request_seq = 0;
        rc = zlink_router_recv_part (handle, &peer_rid, &spot_rid, &request_seq,
                                     &part, &has_more, ZLINK_DONTWAIT);
    } else {
        const zlink_routing_id_t *source_rid = NULL;
        rc = zlink_recv_part (handle, &source_rid, &part, &has_more,
                              ZLINK_DONTWAIT);
    }
    if (rc != ZLINK_RECV_OK) {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            return 1;
        return -1;
    }
    if (has_more == ZLINK_PART_FINAL
        && decode_single_payload (zlink_msg_data (&part), zlink_msg_size (&part),
                                  run_id, &sent_ts)) {
        const uint64_t now = now_ns ();
        const double sample =
          sent_ts > 0 && now >= sent_ts
            ? ((double) (now - sent_ts)) / latency_divisor
            : 0.0;
        *received += 1;
        if (!latency_samples_add (latencies, sample)) {
            zlink_msg_close (&part);
            errno = ENOMEM;
            return -1;
        }
    }
    zlink_msg_close (&part);
    (void) msg_size;
    return 0;
}

static PyObject *
py_multi_echo_roundtrip (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    void **handles = NULL;
    unsigned char *payloads = NULL;
    unsigned char *awaiting_reply = NULL;
    unsigned char *send_pending = NULL;
    zlink_pollitem_t *items = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    int router_mode = 0;
    Py_buffer rid_view = { 0 };
    zlink_routing_id_t server_rid;
    unsigned long long received = 0;
    uint64_t seq = 1;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = { 0 };

    (void) self;
    memset (&server_rid, 0, sizeof (server_rid));
    if (!PyArg_ParseTuple (args, "OniKiy*", &handles_obj, &msg_size,
                           &duration_s, &run_id_value, &router_mode, &rid_view))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyBuffer_Release (&rid_view);
        PyErr_SetString (PyExc_ValueError, "invalid multi echo arguments");
        return NULL;
    }
    if (copy_routing_id (&rid_view, &server_rid) != 0) {
        PyBuffer_Release (&rid_view);
        return NULL;
    }
    PyBuffer_Release (&rid_view);
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq)
        return NULL;
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }

    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    payloads = (unsigned char *) malloc ((size_t) handle_count * (size_t) msg_size);
    awaiting_reply = (unsigned char *) calloc ((size_t) handle_count, 1);
    send_pending = (unsigned char *) calloc ((size_t) handle_count, 1);
    items = (zlink_pollitem_t *) calloc ((size_t) handle_count,
                                         sizeof (zlink_pollitem_t));
    if (!handles || !payloads || !awaiting_reply || !send_pending || !items) {
        free (handles);
        free (payloads);
        free (awaiting_reply);
        free (send_pending);
        free (items);
        Py_DECREF (handles_seq);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            free (payloads);
            free (awaiting_reply);
            free (send_pending);
            free (items);
            Py_DECREF (handles_seq);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
        memset (payloads + ((size_t) i * (size_t) msg_size), 'm',
                (size_t) msg_size);
        send_pending[i] = 1;
    }
    Py_DECREF (handles_seq);

    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline && ok) {
        int any_interest = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if (awaiting_reply[i] || !send_pending[i])
                continue;
            unsigned char *payload =
              payloads + ((size_t) i * (size_t) msg_size);
            if (!stamp_single_payload (payload, (size_t) msg_size,
                                       (uint32_t) run_id_value, 1, seq)) {
                ok = 0;
                err = EINVAL;
                break;
            }
            int send_rc = router_mode
                            ? send_payload_one_part_rid (
                                handles[i], &server_rid, payload,
                                (size_t) msg_size, ZLINK_DONTWAIT)
                            : send_payload_one_part (handles[i], payload,
                                                     (size_t) msg_size,
                                                     ZLINK_DONTWAIT);
            if (send_rc < 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            if (send_rc == 0) {
                awaiting_reply[i] = 1;
                send_pending[i] = 0;
                ++seq;
            }
        }
        if (!ok)
            break;

        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            short events = 0;
            if (awaiting_reply[i])
                events |= ZLINK_POLLIN;
            else if (send_pending[i])
                events |= ZLINK_POLLOUT;
            items[i].socket = handles[i];
            items[i].fd = 0;
            items[i].events = events;
            items[i].revents = 0;
            if (events)
                any_interest = 1;
        }
        if (!any_interest) {
            for (Py_ssize_t i = 0; i < handle_count; ++i) {
                if (!awaiting_reply[i])
                    send_pending[i] = 1;
            }
            continue;
        }
        uint64_t now_steady = steady_ns ();
        if (now_steady >= deadline)
            break;
        long remaining_ms = (long) ((deadline - now_steady) / 1000000ULL);
        if (remaining_ms <= 0)
            remaining_ms = 1;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int poll_rc = zlink_poll (items, (int) handle_count, remaining_ms,
                                  &poll_error);
        if (poll_rc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            ok = 0;
            err = zlink_errno ();
            if (err == 0 && poll_error != ZLINK_CONFIG_OK)
                err = EINVAL;
            break;
        }
        if (poll_rc == 0)
            continue;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if ((items[i].revents & ZLINK_POLLOUT) && !awaiting_reply[i]
                && send_pending[i]) {
                unsigned char *payload =
                  payloads + ((size_t) i * (size_t) msg_size);
                if (!stamp_single_payload (payload, (size_t) msg_size,
                                           (uint32_t) run_id_value, 1, seq)) {
                    ok = 0;
                    err = EINVAL;
                    break;
                }
                int send_rc = router_mode
                                ? send_payload_one_part_rid (
                                    handles[i], &server_rid, payload,
                                    (size_t) msg_size, ZLINK_DONTWAIT)
                                : send_payload_one_part (handles[i], payload,
                                                         (size_t) msg_size,
                                                         ZLINK_DONTWAIT);
                if (send_rc < 0) {
                    ok = 0;
                    err = zlink_errno ();
                    break;
                }
                if (send_rc == 0) {
                    awaiting_reply[i] = 1;
                    send_pending[i] = 0;
                    ++seq;
                }
            }
            if (!(items[i].revents & ZLINK_POLLIN))
                continue;
            while (1) {
                int recv_rc = recv_echo_one_part (
                  handles[i], router_mode, (uint32_t) run_id_value,
                  (size_t) msg_size, 2.0, &latencies, &received);
                if (recv_rc == 1)
                    break;
                if (recv_rc < 0) {
                    ok = 0;
                    err = errno ? errno : zlink_errno ();
                    break;
                }
                awaiting_reply[i] = 0;
                if (steady_ns () < deadline)
                    send_pending[i] = 1;
            }
            if (!ok)
                break;
        }
    }
    Py_END_ALLOW_THREADS

    free (handles);
    free (payloads);
    free (awaiting_reply);
    free (send_pending);
    free (items);
    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0,
                              ok ? 0 : err);
    }
    qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms = (latencies.sum / (double) latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size * 2.0)
                       / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms,
                          p95_ms, p99_ms, 0);
}

static int
recv_spot_echo_one_part (void *handle, uint32_t run_id, size_t msg_size,
                         latency_samples_t *latencies,
                         unsigned long long *received)
{
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_routing_id_t *node_rid = NULL;
    const zlink_routing_id_t *spot_rid = NULL;
    uint64_t request_seq = 0;
    int rc = ZLINK_RECV_OK;

    if (zlink_msg_init (&part) != 0)
        return -1;
    rc = zlink_spot_recv_part (handle, &node_rid, &spot_rid, &request_seq,
                               &part, &has_more, ZLINK_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        int err = zlink_errno ();
        if (err == 0)
            err = errno;
        zlink_msg_close (&part);
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            return 1;
        errno = err;
        return -1;
    }
    if (has_more != ZLINK_PART_FINAL || request_seq != 0) {
        zlink_msg_close (&part);
        return 0;
    }
    {
        uint64_t sent_ts = 0;
        const void *data = zlink_msg_data (&part);
        size_t size = zlink_msg_size (&part);
        if (decode_single_payload (data, size, run_id, &sent_ts)) {
            const uint64_t now = now_ns ();
            const double sample =
              sent_ts > 0 && now >= sent_ts ? ((double) (now - sent_ts)) / 2.0
                                             : 0.0;
            if (!latency_samples_add (latencies, sample)) {
                zlink_msg_close (&part);
                errno = ENOMEM;
                return -1;
            }
            ++(*received);
        }
    }
    zlink_msg_close (&part);
    (void) msg_size;
    (void) node_rid;
    (void) spot_rid;
    return 0;
}

static PyObject *
py_multi_spot_sendsend_roundtrip (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    PyObject *node_rid_obj = NULL;
    PyObject *spot_rid_obj = NULL;
    Py_buffer node_view = { 0 };
    Py_buffer spot_view = { 0 };
    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    void **handles = NULL;
    unsigned char *payloads = NULL;
    unsigned char *awaiting_reply = NULL;
    unsigned char *send_pending = NULL;
    zlink_poller_event_t *events = NULL;
    void *poller = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    unsigned long long received = 0;
    uint64_t seq = 1;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = { 0 };

    (void) self;
    memset (&node_rid, 0, sizeof (node_rid));
    memset (&spot_rid, 0, sizeof (spot_rid));
    if (!PyArg_ParseTuple (args, "OniKOO", &handles_obj, &msg_size,
                           &duration_s, &run_id_value, &node_rid_obj,
                           &spot_rid_obj))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyErr_SetString (PyExc_ValueError, "invalid multi spot arguments");
        return NULL;
    }
    if (PyObject_GetBuffer (node_rid_obj, &node_view, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (copy_routing_id (&node_view, &node_rid) != 0) {
        PyBuffer_Release (&node_view);
        return NULL;
    }
    PyBuffer_Release (&node_view);
    if (PyObject_GetBuffer (spot_rid_obj, &spot_view, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (copy_routing_id (&spot_view, &spot_rid) != 0) {
        PyBuffer_Release (&spot_view);
        return NULL;
    }
    PyBuffer_Release (&spot_view);

    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq)
        return NULL;
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    payloads = (unsigned char *) malloc ((size_t) handle_count * (size_t) msg_size);
    awaiting_reply = (unsigned char *) calloc ((size_t) handle_count, 1);
    send_pending = (unsigned char *) calloc ((size_t) handle_count, 1);
    events = (zlink_poller_event_t *) calloc ((size_t) handle_count,
                                              sizeof (zlink_poller_event_t));
    poller = zlink_poller_new ();
    if (!handles || !payloads || !awaiting_reply || !send_pending || !events
        || !poller) {
        free (handles);
        free (payloads);
        free (awaiting_reply);
        free (send_pending);
        free (events);
        if (poller)
            zlink_poller_destroy (&poller);
        Py_DECREF (handles_seq);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            free (payloads);
            free (awaiting_reply);
            free (send_pending);
            free (events);
            zlink_poller_destroy (&poller);
            Py_DECREF (handles_seq);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
        memset (payloads + ((size_t) i * (size_t) msg_size), 's',
                (size_t) msg_size);
        send_pending[i] = 1;
        if (zlink_poller_add (poller, handles[i],
                              (void *) (uintptr_t) ((size_t) i + 1),
                              ZLINK_POLLIN)
            != ZLINK_CONFIG_OK) {
            int add_err = zlink_errno ();
            free (handles);
            free (payloads);
            free (awaiting_reply);
            free (send_pending);
            free (events);
            zlink_poller_destroy (&poller);
            Py_DECREF (handles_seq);
            PyErr_Format (PyExc_RuntimeError,
                          "failed to register SPOT poller slot: %d", add_err);
            return NULL;
        }
    }
    Py_DECREF (handles_seq);

    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline && ok) {
        int any_waiting = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if (awaiting_reply[i]) {
                any_waiting = 1;
                continue;
            }
            if (!send_pending[i])
                continue;
            unsigned char *payload =
              payloads + ((size_t) i * (size_t) msg_size);
            zlink_msg_t part;
            if (!stamp_single_payload (payload, (size_t) msg_size,
                                       (uint32_t) run_id_value, 1, seq)) {
                ok = 0;
                err = EINVAL;
                break;
            }
            if (msg_size <= 256) {
                if (zlink_msg_init_data (&part, payload, (size_t) msg_size, NULL,
                                         NULL)
                    != ZLINK_CONFIG_OK) {
                    ok = 0;
                    err = EINVAL;
                    break;
                }
            } else {
                if (zlink_msg_init_size (&part, (size_t) msg_size)
                    != ZLINK_CONFIG_OK) {
                    ok = 0;
                    err = EINVAL;
                    break;
                }
                memcpy (zlink_msg_data (&part), payload, (size_t) msg_size);
            }
            int send_rc =
              zlink_spot_send_spot_part (handles[i], &node_rid, &spot_rid,
                                         &part, ZLINK_DONTWAIT,
                                         ZLINK_PART_FINAL);
            if (send_rc == ZLINK_SUBMIT_OK) {
                awaiting_reply[i] = 1;
                send_pending[i] = 0;
                any_waiting = 1;
                ++seq;
            } else {
                int send_err = zlink_errno ();
                if (send_err == 0)
                    send_err = errno;
                if (!(send_err == EAGAIN || send_err == EWOULDBLOCK
                      || send_err == EINTR || send_err == ENOTCONN
                      || send_err == ETIMEDOUT)) {
                    ok = 0;
                    err = send_err;
                    zlink_msg_close (&part);
                    break;
                }
            }
            zlink_msg_close (&part);
        }
        if (!ok)
            break;
        if (!any_waiting) {
            for (Py_ssize_t i = 0; i < handle_count; ++i) {
                if (!awaiting_reply[i])
                    send_pending[i] = 1;
            }
            continue;
        }
        uint64_t now_steady = steady_ns ();
        if (now_steady >= deadline)
            break;
        long remaining_ms = (long) ((deadline - now_steady) / 1000000ULL);
        if (remaining_ms <= 0)
            remaining_ms = 1;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int poll_rc =
          zlink_poller_wait (poller, events, (int) handle_count,
                             remaining_ms > 50 ? 50 : remaining_ms,
                             &poll_error);
        if (poll_rc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            ok = 0;
            err = zlink_errno ();
            if (err == 0 && poll_error != ZLINK_CONFIG_OK)
                err = EINVAL;
            break;
        }
        if (poll_rc == 0)
            continue;
        for (int event_index = 0; event_index < poll_rc; ++event_index) {
            uintptr_t user_index = (uintptr_t) events[event_index].user_data;
            if (user_index == 0 || user_index > (uintptr_t) handle_count)
                continue;
            Py_ssize_t i = (Py_ssize_t) user_index - 1;
            if (!(events[event_index].events & ZLINK_POLLIN))
                continue;
            while (1) {
                int recv_rc = recv_spot_echo_one_part (
                  handles[i], (uint32_t) run_id_value, (size_t) msg_size,
                  &latencies, &received);
                if (recv_rc == 1)
                    break;
                if (recv_rc < 0) {
                    ok = 0;
                    err = errno ? errno : zlink_errno ();
                    break;
                }
                awaiting_reply[i] = 0;
                if (steady_ns () < deadline)
                    send_pending[i] = 1;
            }
            if (!ok)
                break;
        }
    }
    Py_END_ALLOW_THREADS

    free (handles);
    free (payloads);
    free (awaiting_reply);
    free (send_pending);
    free (events);
    if (poller)
        zlink_poller_destroy (&poller);
    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0,
                              ok ? 0 : err);
    }
    qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms = (latencies.sum / (double) latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size * 2.0)
                       / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms,
                          p95_ms, p99_ms, 0);
}

static void
spot_reqrep_reply_handler (zlink_request_result_t result, zlink_msg_t *parts,
                           size_t part_count, void *userdata)
{
    spot_reqrep_client_slot_t *slot =
      (spot_reqrep_client_slot_t *) userdata;
    if (!slot || !slot->owner)
        return;

    spot_reqrep_client_state_t *state = slot->owner;
    slot->waiting = 0;
    if (result != ZLINK_REQUEST_OK || !parts || part_count == 0)
        return;

    uint64_t sent_ts = 0;
    const void *data = zlink_msg_data (&parts[0]);
    const size_t size = zlink_msg_size (&parts[0]);
    if (!decode_single_payload (data, size, state->run_id, &sent_ts))
        return;

    const uint64_t current_steady = steady_ns ();
    if (current_steady >= state->deadline_ns || sent_ts == 0)
        return;

    const uint64_t current_ns = now_ns ();
    if (current_ns < sent_ts)
        return;

    const double sample = (double) (current_ns - sent_ts) / 2.0;
    pthread_mutex_lock (&state->lock);
    if (!latency_samples_add (&state->latencies, sample)) {
        state->failed = 1;
        state->last_errno = ENOMEM;
    } else {
        ++state->received;
    }
    pthread_mutex_unlock (&state->lock);
}

static int
spot_reqrep_submit_one (spot_reqrep_client_slot_t *slot,
                        const zlink_routing_id_t *node_rid,
                        const zlink_routing_id_t *spot_rid, uint32_t timeout_ms)
{
    zlink_msg_t part;
    spot_reqrep_client_state_t *state = slot->owner;
    if (!stamp_single_payload (slot->payload, state->msg_size, state->run_id, 1,
                               slot->seq))
        return -1;
    if (zlink_msg_init_size (&part, state->msg_size) != ZLINK_CONFIG_OK)
        return -1;
    memcpy (zlink_msg_data (&part), slot->payload, state->msg_size);
    slot->waiting = 1;
    const int rc = zlink_spot_request_spot_part (
      slot->handle, node_rid, spot_rid, &part, spot_reqrep_reply_handler, slot,
      ZLINK_DONTWAIT, ZLINK_PART_FINAL, timeout_ms);
    if (rc == ZLINK_SUBMIT_OK) {
        ++slot->seq;
        return 0;
    }
    slot->waiting = 0;
    zlink_msg_close (&part);
    {
        int err = zlink_errno ();
        if (err == 0)
            err = errno;
        if (rc == ZLINK_SUBMIT_BACKPRESSURED || rc == ZLINK_SUBMIT_NOT_CONNECTED
            || err == EAGAIN || err == EWOULDBLOCK || err == EINTR
            || err == ENOTCONN || err == ETIMEDOUT)
            return 1;
        errno = err;
        return -1;
    }
}

static PyObject *
py_multi_spot_reqrep_roundtrip (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    Py_buffer node_rid_view = { 0 };
    Py_buffer spot_rid_view = { 0 };
    void **handles = NULL;
    spot_reqrep_client_slot_t *slots = NULL;
    zlink_poller_event_t *events = NULL;
    void *poller = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    spot_reqrep_client_state_t state;
    int ok = 1;
    int err = 0;
    int pending_after_drain = 0;

    (void) self;
    memset (&state, 0, sizeof (state));
    if (!PyArg_ParseTuple (args, "OniKy*y*", &handles_obj, &msg_size,
                           &duration_s, &run_id_value, &node_rid_view,
                           &spot_rid_view))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyBuffer_Release (&node_rid_view);
        PyBuffer_Release (&spot_rid_view);
        PyErr_SetString (PyExc_ValueError,
                         "invalid SPOT reqrep roundtrip arguments");
        return NULL;
    }
    if (copy_routing_id (&node_rid_view, &node_rid) != 0
        || copy_routing_id (&spot_rid_view, &spot_rid) != 0) {
        PyBuffer_Release (&node_rid_view);
        PyBuffer_Release (&spot_rid_view);
        return NULL;
    }
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq) {
        PyBuffer_Release (&node_rid_view);
        PyBuffer_Release (&spot_rid_view);
        return NULL;
    }
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&node_rid_view);
        PyBuffer_Release (&spot_rid_view);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    slots = (spot_reqrep_client_slot_t *) calloc (
      (size_t) handle_count, sizeof (spot_reqrep_client_slot_t));
    events = (zlink_poller_event_t *) calloc ((size_t) handle_count,
                                             sizeof (zlink_poller_event_t));
    if (!handles || !slots || !events) {
        free (handles);
        free (slots);
        free (events);
        Py_DECREF (handles_seq);
        PyBuffer_Release (&node_rid_view);
        PyBuffer_Release (&spot_rid_view);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            ok = 0;
            break;
        }
        handles[i] = (void *) (uintptr_t) value;
    }
    Py_DECREF (handles_seq);
    if (!ok) {
        free (handles);
        free (slots);
        free (events);
        PyBuffer_Release (&node_rid_view);
        PyBuffer_Release (&spot_rid_view);
        return NULL;
    }

    state.run_id = (uint32_t) run_id_value;
    state.msg_size = (size_t) msg_size;
    state.deadline_ns = steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    pthread_mutex_init (&state.lock, NULL);

    poller = zlink_poller_new ();
    if (!poller) {
        ok = 0;
        err = zlink_errno ();
    }
    for (Py_ssize_t i = 0; ok && i < handle_count; ++i) {
        slots[i].owner = &state;
        slots[i].handle = handles[i];
        slots[i].seq = 1;
        slots[i].payload = (unsigned char *) malloc ((size_t) msg_size);
        if (!slots[i].payload) {
            ok = 0;
            err = ENOMEM;
            break;
        }
        memset (slots[i].payload, 'c', (size_t) msg_size);
        zlink_config_result_t add_rc =
          zlink_poller_add (poller, handles[i], &slots[i],
                            ZLINK_POLLCOMPLETION);
        if (add_rc != ZLINK_CONFIG_OK) {
            ok = 0;
            err = zlink_errno ();
            if (err == 0)
                err = EINVAL;
            break;
        }
    }

    const uint32_t timeout_ms = 200;
    const Py_ssize_t active_slots =
      msg_size >= 131072 ? (handle_count < 8 ? handle_count : 8)
      : msg_size >= 65536 ? (handle_count < 32 ? handle_count : 32)
                          : handle_count;

    Py_BEGIN_ALLOW_THREADS
    while (ok && !state.failed && steady_ns () < state.deadline_ns) {
        int submitted = 0;
        int waiting = 0;
        for (Py_ssize_t i = 0; i < active_slots; ++i) {
            if (slots[i].waiting) {
                waiting = 1;
                continue;
            }
            int step =
              spot_reqrep_submit_one (&slots[i], &node_rid, &spot_rid,
                                      timeout_ms);
            if (step == 0) {
                submitted = 1;
                waiting = 1;
            } else if (step < 0) {
                ok = 0;
                err = errno ? errno : zlink_errno ();
                break;
            }
        }
        if (!ok || submitted)
            continue;
        uint64_t current = steady_ns ();
        if (current >= state.deadline_ns)
            break;
        long wait_ms = (long) ((state.deadline_ns - current) / 1000000ULL);
        if (wait_ms <= 0)
            wait_ms = 1;
        if (wait_ms > 50)
            wait_ms = 50;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int event_count =
          zlink_poller_wait (poller, events, (int) handle_count, (int) wait_ms,
                             &poll_error);
        if (event_count < 0) {
            int poll_errno = zlink_errno ();
            if (poll_errno == EINTR)
                continue;
            ok = 0;
            err = poll_errno ? poll_errno
                             : (poll_error != ZLINK_CONFIG_OK ? EINVAL : errno);
            break;
        }
        (void) waiting;
    }
    const uint64_t drain_deadline = steady_ns () + 1000000000ULL;
    while (ok && steady_ns () < drain_deadline) {
        int any_waiting = 0;
        for (Py_ssize_t i = 0; i < active_slots; ++i) {
            if (slots[i].waiting) {
                any_waiting = 1;
                break;
            }
        }
        if (!any_waiting)
            break;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int event_count =
          zlink_poller_wait (poller, events, (int) handle_count, 50,
                             &poll_error);
        if (event_count < 0 && zlink_errno () != EINTR) {
            ok = 0;
            err = zlink_errno ();
            break;
        }
    }
    Py_END_ALLOW_THREADS

    if (state.failed) {
        ok = 0;
        err = state.last_errno ? state.last_errno : ENOMEM;
    }
    for (Py_ssize_t i = 0; i < active_slots; ++i) {
        if (slots[i].waiting) {
            pending_after_drain = 1;
            break;
        }
    }
    if (pending_after_drain) {
        ok = 0;
        err = ETIMEDOUT;
        Py_BEGIN_ALLOW_THREADS
        while (pending_after_drain) {
            pending_after_drain = 0;
            for (Py_ssize_t i = 0; i < active_slots; ++i) {
                if (slots[i].waiting) {
                    pending_after_drain = 1;
                    break;
                }
            }
            if (!pending_after_drain)
                break;
            zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
            int event_count =
              zlink_poller_wait (poller, events, (int) handle_count, 50,
                                 &poll_error);
            if (event_count < 0 && zlink_errno () != EINTR) {
                struct timespec ts = { 0, 1000000L };
                nanosleep (&ts, NULL);
            }
        }
        Py_END_ALLOW_THREADS
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i)
        free (slots[i].payload);
    free (slots);
    free (handles);
    free (events);
    if (poller)
        zlink_poller_destroy (&poller);
    PyBuffer_Release (&node_rid_view);
    PyBuffer_Release (&spot_rid_view);
    pthread_mutex_destroy (&state.lock);

    if (!ok || state.received == 0) {
        free (state.latencies.values);
        return Py_BuildValue ("Kdddddi", state.received, 0.0, 0.0, 0.0, 0.0,
                              0.0, ok ? 0 : err);
    }
    qsort (state.latencies.values, state.latencies.count, sizeof (double),
           compare_double);
    double mean_ms =
      (state.latencies.sum / (double) state.latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&state.latencies, 0.95)
                    / 1000000.0;
    double p99_ms = latency_percentile_sorted (&state.latencies, 0.99)
                    / 1000000.0;
    double throughput = (double) state.received / (double) duration_s;
    double bandwidth = ((double) state.received * (double) msg_size * 2.0)
                       / (double) duration_s / 1000000.0;
    free (state.latencies.values);
    return Py_BuildValue ("Kdddddi", state.received, throughput, bandwidth,
                          mean_ms, p95_ms, p99_ms, 0);
}

static void
spot_routed_echo_dispatch_handler (void *spot,
                                   const zlink_spot_dispatch_info_t *info,
                                   void *userdata)
{
    spot_routed_echo_session_t *session =
      (spot_routed_echo_session_t *) userdata;
    if (!session || !info
        || info->event != ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE)
        return;

    for (;;) {
        const zlink_routing_id_t *node_rid = NULL;
        const zlink_routing_id_t *spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        int rc = 0;

        if (zlink_msg_init (&part) != 0) {
            session->failed = 1;
            session->last_errno = zlink_errno ();
            return;
        }
        rc = zlink_spot_recv_part (spot, &node_rid, &spot_rid, &request_seq,
                                   &part, &has_more, ZLINK_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            int err = zlink_errno ();
            zlink_msg_close (&part);
            if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
                return;
            session->failed = 1;
            session->last_errno = err;
            return;
        }

        if (!node_rid || !spot_rid || has_more != ZLINK_PART_FINAL
            || (session->reply_mode && request_seq == 0)
            || (!session->reply_mode && request_seq != 0)) {
            zlink_msg_close (&part);
            continue;
        }

        ++session->received;
        if (session->reply_mode) {
            rc = zlink_spot_reply_spot_part (spot, node_rid, spot_rid,
                                             request_seq, &part,
                                             ZLINK_PART_FINAL);
        } else {
            rc = zlink_spot_send_spot_part (spot, node_rid, spot_rid, &part,
                                            ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        }
        if (rc == ZLINK_SUBMIT_OK) {
            ++session->sent;
            zlink_msg_close (&part);
            continue;
        }
        {
            int err = zlink_errno ();
            zlink_msg_close (&part);
            if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR
                || err == ENOTCONN || err == ETIMEDOUT)
                continue;
            session->failed = 1;
            session->last_errno = err;
            return;
        }
    }
}

static void
spot_routed_echo_capsule_destructor (PyObject *capsule)
{
    spot_routed_echo_session_t *session =
      (spot_routed_echo_session_t *) PyCapsule_GetPointer (
        capsule, "zlink.spot_routed_echo");
    free (session);
}

static PyObject *
py_spot_routed_echo_install (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int reply_mode = 0;
    spot_routed_echo_session_t *session = NULL;
    int rc = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &reply_mode))
        return NULL;
    session = (spot_routed_echo_session_t *) calloc (1, sizeof (*session));
    if (!session) {
        PyErr_NoMemory ();
        return NULL;
    }
    session->handle = (void *) (uintptr_t) handle_value;
    session->reply_mode = reply_mode ? 1 : 0;

    Py_BEGIN_ALLOW_THREADS
    rc = zlink_spot_dispatch_event_handler (
      session->handle, spot_routed_echo_dispatch_handler, session);
    Py_END_ALLOW_THREADS

    if (rc != 0) {
        const int err = zlink_errno ();
        free (session);
        Py_INCREF (Py_None);
        return Py_BuildValue ("NKKKi", Py_None, 0ULL, 0ULL, 0ULL, err);
    }

    PyObject *capsule =
      PyCapsule_New (session, "zlink.spot_routed_echo",
                     spot_routed_echo_capsule_destructor);
    if (!capsule) {
        free (session);
        return NULL;
    }
    return Py_BuildValue ("NKKKi", capsule, session->received, session->sent,
                          (unsigned long long) session->failed, 0);
}

static PyObject *
py_spot_routed_echo_stats (PyObject *self, PyObject *args)
{
    PyObject *capsule = NULL;
    spot_routed_echo_session_t *session = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "O", &capsule))
        return NULL;
    session = (spot_routed_echo_session_t *) PyCapsule_GetPointer (
      capsule, "zlink.spot_routed_echo");
    if (!session)
        return NULL;
    return Py_BuildValue ("KKKi", session->received, session->sent,
                          (unsigned long long) session->failed,
                          session->last_errno);
}

static void
routed_echo_free_queue (routed_echo_queue_t *queue)
{
    routed_echo_item_t *item = NULL;
    routed_echo_item_t *next = NULL;

    if (!queue)
        return;
    item = queue->head;
    while (item) {
        next = item->next;
        zlink_msg_close (&item->msg);
        free (item);
        item = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->pending_count = 0;
}

static int
routed_echo_enqueue (routed_echo_queue_t *queue,
                     const zlink_routing_id_t *routing_id, zlink_msg_t *msg)
{
    routed_echo_item_t *item = NULL;

    if (!queue || !routing_id || !msg)
        return -1;
    item = (routed_echo_item_t *) calloc (1, sizeof (*item));
    if (!item)
        return -1;
    item->routing_id = *routing_id;
    item->msg = *msg;
    if (queue->tail)
        queue->tail->next = item;
    else
        queue->head = item;
    queue->tail = item;
    queue->pending_count++;
    return 0;
}

static int
routed_echo_try_send (void *handle, const zlink_routing_id_t *routing_id,
                      zlink_msg_t *msg)
{
    int rc = zlink_send_part_rid (handle, routing_id, msg, ZLINK_DONTWAIT,
                                  ZLINK_PART_FINAL);
    if (rc == ZLINK_SUBMIT_OK)
        return 0;
    {
        int err = zlink_errno ();
        if (err == 0)
            err = errno;
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
            || err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH
            || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static int
routed_echo_drain_pending (void *handle, routed_echo_queue_t *queue,
                           unsigned long long *sent)
{
    while (queue->head) {
        routed_echo_item_t *item = queue->head;
        int send_rc = routed_echo_try_send (handle, &item->routing_id,
                                            &item->msg);
        if (send_rc > 0)
            return 1;
        if (send_rc < 0)
            return -1;
        queue->head = item->next;
        if (!queue->head)
            queue->tail = NULL;
        queue->pending_count--;
        ++(*sent);
        zlink_msg_close (&item->msg);
        free (item);
    }
    return 0;
}

static int
routed_echo_recv_and_reply (void *handle, routed_echo_queue_t *queue,
                            unsigned long long *received,
                            unsigned long long *sent)
{
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_routing_id_t *peer_rid = NULL;
    const zlink_routing_id_t *spot_rid = NULL;
    uint64_t request_seq = 0;
    int rc = ZLINK_RECV_OK;

    if (zlink_msg_init (&part) != 0)
        return -1;
    rc = zlink_router_recv_part (handle, &peer_rid, &spot_rid, &request_seq,
                                 &part, &has_more, ZLINK_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        int err = zlink_errno ();
        if (err == 0)
            err = errno;
        zlink_msg_close (&part);
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            return 1;
        return -1;
    }
    if (!peer_rid || peer_rid->size == 0 || has_more != ZLINK_PART_FINAL) {
        zlink_msg_close (&part);
        errno = EINVAL;
        return -1;
    }
    (void) spot_rid;
    (void) request_seq;
    ++(*received);
    if (!queue->head) {
        int send_rc = routed_echo_try_send (handle, peer_rid, &part);
        if (send_rc == 0) {
            ++(*sent);
            zlink_msg_close (&part);
            return 0;
        }
        if (send_rc < 0) {
            zlink_msg_close (&part);
            return -1;
        }
    }
    if (routed_echo_enqueue (queue, peer_rid, &part) != 0) {
        zlink_msg_close (&part);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static PyObject *
py_router_echo_server_loop (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_buffer stop_flag = { 0 };
    void *handle = NULL;
    routed_echo_queue_t queue = { 0 };
    zlink_pollitem_t item;
    unsigned long long received = 0;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Kw*", &handle_value, &stop_flag))
        return NULL;
    if (stop_flag.len < 1) {
        PyBuffer_Release (&stop_flag);
        PyErr_SetString (PyExc_ValueError, "stop flag must not be empty");
        return NULL;
    }
    handle = (void *) (uintptr_t) handle_value;

    Py_BEGIN_ALLOW_THREADS
    while (!*((volatile unsigned char *) stop_flag.buf)) {
        int progressed = 0;
        int had_pending = queue.head != NULL;
        int drain_rc = routed_echo_drain_pending (handle, &queue, &sent);
        if (drain_rc < 0) {
            ok = 0;
            err = zlink_errno ();
            if (err == 0)
                err = errno;
            break;
        }
        if (had_pending && drain_rc == 0)
            progressed = 1;

        while (!*((volatile unsigned char *) stop_flag.buf)) {
            int recv_rc =
              routed_echo_recv_and_reply (handle, &queue, &received, &sent);
            if (recv_rc == 1)
                break;
            if (recv_rc < 0) {
                ok = 0;
                err = zlink_errno ();
                if (err == 0)
                    err = errno;
                break;
            }
            progressed = 1;
        }
        if (!ok)
            break;
        if (progressed)
            continue;

        item.socket = handle;
        item.fd = 0;
        item.events = ZLINK_POLLIN | (queue.head ? ZLINK_POLLOUT : 0);
        item.revents = 0;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int poll_rc = zlink_poll (&item, 1, 100, &poll_error);
        if (poll_rc < 0) {
            int poll_errno = zlink_errno ();
            if (poll_errno == 0)
                poll_errno = errno;
            if (poll_errno == EINTR || poll_errno == EAGAIN
                || poll_errno == EWOULDBLOCK)
                continue;
            ok = 0;
            err = poll_errno;
            if (err == 0 && poll_error != ZLINK_CONFIG_OK)
                err = EINVAL;
            break;
        }
    }
    Py_END_ALLOW_THREADS

    routed_echo_free_queue (&queue);
    PyBuffer_Release (&stop_flag);
    return Py_BuildValue ("KKKi", received, sent, queue.pending_count,
                          ok ? 0 : err);
}

static PyObject *
py_recv_count_active (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    unsigned long long received = 0;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = { 0 };

    (void) self;
    if (!PyArg_ParseTuple (args, "KniK", &handle_value, &msg_size, &duration_s,
                           &run_id_value))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyErr_SetString (PyExc_ValueError, "invalid receive count arguments");
        return NULL;
    }
    void *handle = (void *) (uintptr_t) handle_value;
    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline) {
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_routing_id_t *source_rid = NULL;
        if (zlink_msg_init (&part) != 0) {
            ok = 0;
            err = zlink_errno ();
            break;
        }
        int rc = zlink_recv_part (handle, &source_rid, &part, &has_more,
                                  ZLINK_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            const int recv_err = zlink_errno ();
            zlink_msg_close (&part);
            if (recv_err == EAGAIN || recv_err == EWOULDBLOCK
                || recv_err == EINTR) {
                struct timespec ts = { 0, 100000L };
                nanosleep (&ts, NULL);
                continue;
            }
            ok = 0;
            err = recv_err;
            break;
        }
        if (has_more == ZLINK_PART_FINAL) {
            uint64_t sent_ts = 0;
            const void *data = zlink_msg_data (&part);
            const size_t size = zlink_msg_size (&part);
            if (decode_single_payload (data, size, (uint32_t) run_id_value,
                                       &sent_ts)) {
                const uint64_t now = now_ns ();
                double sample = sent_ts > 0 && now >= sent_ts
                                  ? (double) (now - sent_ts)
                                  : 0.0;
                ++received;
                if (!latency_samples_add (&latencies, sample)) {
                    ok = 0;
                    err = ENOMEM;
                    zlink_msg_close (&part);
                    break;
                }
            }
        }
        zlink_msg_close (&part);
    }
    Py_END_ALLOW_THREADS

    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0,
                              ok ? 0 : err);
    }
    qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms = (latencies.sum / (double) latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size)
                       / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms,
                          p95_ms, p99_ms, 0);
}

static int
publish_payload_one_part (void *handle, const char *topic, const void *data,
                          size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_publish_part (handle, topic, &part, (zlink_send_flags_t) flags,
                            ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
            || err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH
            || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static PyObject *
py_publish_active (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_buffer topic = { 0 };
    Py_buffer stop_token = { 0 };
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    char topic_text[256];
    unsigned char *payload = NULL;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky*y*niK", &handle_value, &topic,
                           &stop_token, &msg_size, &duration_s,
                           &run_id_value))
        return NULL;
    if (topic.len <= 0 || topic.len >= (Py_ssize_t) sizeof (topic_text)
        || memchr (topic.buf, '\0', (size_t) topic.len) || msg_size < 29
        || duration_s <= 0) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_SetString (PyExc_ValueError, "invalid publish arguments");
        return NULL;
    }
    memcpy (topic_text, topic.buf, (size_t) topic.len);
    topic_text[(size_t) topic.len] = '\0';
    payload = (unsigned char *) malloc ((size_t) msg_size);
    if (!payload) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_NoMemory ();
        return NULL;
    }
    memset (payload, 'p', (size_t) msg_size);
    void *handle = (void *) (uintptr_t) handle_value;
    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    uint64_t seq = 1;

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline) {
        if (!stamp_single_payload (payload, (size_t) msg_size,
                                   (uint32_t) run_id_value, 1, seq)) {
            ok = 0;
            err = EINVAL;
            break;
        }
        int step = publish_payload_one_part (handle, topic_text, payload,
                                             (size_t) msg_size, ZLINK_DONTWAIT);
        if (step < 0) {
            ok = 0;
            err = zlink_errno ();
            break;
        }
        if (step > 0) {
            struct timespec ts = { 0, 100000L };
            nanosleep (&ts, NULL);
            continue;
        }
        ++sent;
        ++seq;
    }
    if (ok && stop_token.len > 0) {
        for (int retry = 0; retry < 1000; ++retry) {
            int step = publish_payload_one_part (handle, topic_text,
                                                 stop_token.buf,
                                                 (size_t) stop_token.len,
                                                 ZLINK_SEND_FLAGS_NONE);
            if (step == 0)
                break;
            if (step < 0 || retry == 999) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            struct timespec ts = { 0, 100000L };
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

    free (payload);
    PyBuffer_Release (&topic);
    PyBuffer_Release (&stop_token);
    return Py_BuildValue ("Ki", sent, ok ? 0 : err);
}

static PyObject *
py_subscribe_count_active (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    Py_buffer expected_topic = { 0 };
    void **handles = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    int sample_stride = 32;
    unsigned long long run_id_value = 0;
    unsigned long long received = 0;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = { 0 };

    (void) self;
    if (!PyArg_ParseTuple (args, "Oy*niKi", &handles_obj, &expected_topic,
                           &msg_size, &duration_s, &run_id_value,
                           &sample_stride))
        return NULL;
    if (expected_topic.len <= 0 || expected_topic.len > 255 || msg_size < 29
        || duration_s <= 0) {
        PyBuffer_Release (&expected_topic);
        PyErr_SetString (PyExc_ValueError, "invalid subscribe count arguments");
        return NULL;
    }
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq) {
        PyBuffer_Release (&expected_topic);
        return NULL;
    }
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&expected_topic);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    if (!handles) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&expected_topic);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            Py_DECREF (handles_seq);
            PyBuffer_Release (&expected_topic);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
    }
    if (sample_stride <= 0)
        sample_stride = 32;
    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline) {
        int progressed = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            zlink_msg_t part;
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            const zlink_routing_id_t *source_rid = NULL;
            char topic[256];
            size_t topic_len = 0;
            if (steady_ns () >= deadline)
                break;
            if (zlink_msg_init (&part) != 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            int rc = zlink_subscribe_part (
              handles[i], &source_rid, topic, sizeof (topic), &topic_len,
              &part, &has_more, ZLINK_DONTWAIT);
            if (rc != ZLINK_RECV_OK) {
                const int recv_err = zlink_errno ();
                zlink_msg_close (&part);
                if (recv_err == EAGAIN || recv_err == EWOULDBLOCK
                    || recv_err == EINTR)
                    continue;
                ok = 0;
                err = recv_err;
                break;
            }
            progressed = 1;
            if (has_more == ZLINK_PART_FINAL
                && topic_len == (size_t) expected_topic.len
                && memcmp (topic, expected_topic.buf, topic_len) == 0) {
                uint64_t sent_ts = 0;
                const void *data = zlink_msg_data (&part);
                const size_t size = zlink_msg_size (&part);
                if (decode_single_payload (data, size, (uint32_t) run_id_value,
                                           &sent_ts)) {
                    ++received;
                    if (received == 1
                        || received % (unsigned long long) sample_stride == 0) {
                        const uint64_t now = now_ns ();
                        double sample = sent_ts > 0 && now >= sent_ts
                                          ? (double) (now - sent_ts)
                                          : 0.0;
                        if (!latency_samples_add (&latencies, sample)) {
                            ok = 0;
                            err = ENOMEM;
                            zlink_msg_close (&part);
                            break;
                        }
                    }
                }
            }
            zlink_msg_close (&part);
        }
        if (!ok)
            break;
        if (!progressed) {
            struct timespec ts = { 0, 100000L };
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

    free (handles);
    Py_DECREF (handles_seq);
    PyBuffer_Release (&expected_topic);
    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0,
                              ok ? 0 : err);
    }
    if (latencies.count > 0)
        qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms = latencies.count > 0
                       ? (latencies.sum / (double) latencies.count) / 1000000.0
                       : 0.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size)
                       / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms,
                          p95_ms, p99_ms, 0);
}

static PyObject *
py_spot_subscribe_count_active (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    Py_buffer expected_topic = { 0 };
    void **handles = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    int sample_stride = 32;
    unsigned long long run_id_value = 0;
    unsigned long long received = 0;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = { 0 };

    (void) self;
    if (!PyArg_ParseTuple (args, "Oy*niKi", &handles_obj, &expected_topic,
                           &msg_size, &duration_s, &run_id_value,
                           &sample_stride))
        return NULL;
    if (expected_topic.len <= 0 || expected_topic.len > 255 || msg_size < 29
        || duration_s <= 0) {
        PyBuffer_Release (&expected_topic);
        PyErr_SetString (PyExc_ValueError,
                         "invalid SPOT subscribe count arguments");
        return NULL;
    }
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq) {
        PyBuffer_Release (&expected_topic);
        return NULL;
    }
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&expected_topic);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    if (!handles) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&expected_topic);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            Py_DECREF (handles_seq);
            PyBuffer_Release (&expected_topic);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
    }
    if (sample_stride <= 0)
        sample_stride = 32;
    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline) {
        int progressed = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            zlink_msg_t part;
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            const zlink_routing_id_t *source_rid = NULL;
            char topic[256];
            size_t topic_len = 0;
            if (steady_ns () >= deadline)
                break;
            if (zlink_msg_init (&part) != 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            int rc = zlink_spot_subscribe_part (
              handles[i], &source_rid, topic, sizeof (topic), &topic_len,
              &part, &has_more, ZLINK_DONTWAIT);
            if (rc != ZLINK_RECV_OK) {
                const int recv_err = zlink_errno ();
                zlink_msg_close (&part);
                if (recv_err == EAGAIN || recv_err == EWOULDBLOCK
                    || recv_err == EINTR)
                    continue;
                ok = 0;
                err = recv_err;
                break;
            }
            progressed = 1;
            if (has_more == ZLINK_PART_FINAL
                && topic_len == (size_t) expected_topic.len
                && memcmp (topic, expected_topic.buf, topic_len) == 0) {
                uint64_t sent_ts = 0;
                const void *data = zlink_msg_data (&part);
                const size_t size = zlink_msg_size (&part);
                if (decode_single_payload (data, size, (uint32_t) run_id_value,
                                           &sent_ts)) {
                    ++received;
                    if (received == 1
                        || received % (unsigned long long) sample_stride == 0) {
                        const uint64_t now = now_ns ();
                        double sample = sent_ts > 0 && now >= sent_ts
                                          ? (double) (now - sent_ts)
                                          : 0.0;
                        if (!latency_samples_add (&latencies, sample)) {
                            ok = 0;
                            err = ENOMEM;
                            zlink_msg_close (&part);
                            break;
                        }
                    }
                }
            }
            zlink_msg_close (&part);
        }
        if (!ok)
            break;
        if (!progressed) {
            struct timespec ts = { 0, 100000L };
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

    free (handles);
    Py_DECREF (handles_seq);
    PyBuffer_Release (&expected_topic);
    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0,
                              ok ? 0 : err);
    }
    if (latencies.count > 0)
        qsort (latencies.values, latencies.count, sizeof (double),
               compare_double);
    double mean_ms = latencies.count > 0
                       ? (latencies.sum / (double) latencies.count) / 1000000.0
                       : 0.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size)
                       / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms,
                          p95_ms, p99_ms, 0);
}

static int
spot_publish_one_part (void *spot, const char *topic, const void *data,
                       size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_spot_publish_part (spot, topic, &part, (zlink_send_flags_t) flags,
                                 ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
            || err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH
            || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static PyObject *
py_spot_publish_active (PyObject *self, PyObject *args)
{
    unsigned long long publisher_value = 0;
    unsigned long long stop_publisher_value = 0;
    Py_buffer topic = { 0 };
    Py_buffer stop_token = { 0 };
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    char topic_text[256];
    unsigned char *payload = NULL;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "KKy*y*niK", &publisher_value,
                           &stop_publisher_value, &topic, &stop_token,
                           &msg_size, &duration_s, &run_id_value))
        return NULL;
    if (topic.len <= 0 || topic.len >= (Py_ssize_t) sizeof (topic_text)
        || memchr (topic.buf, '\0', (size_t) topic.len) || msg_size < 29
        || duration_s <= 0) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_SetString (PyExc_ValueError, "invalid SPOT publish arguments");
        return NULL;
    }

    memcpy (topic_text, topic.buf, (size_t) topic.len);
    topic_text[(size_t) topic.len] = '\0';
    payload = (unsigned char *) malloc ((size_t) msg_size);
    if (!payload) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_NoMemory ();
        return NULL;
    }
    memset (payload, 's', (size_t) msg_size);

    void *publisher = (void *) (uintptr_t) publisher_value;
    void *stop_publisher = (void *) (uintptr_t) stop_publisher_value;
    const uint64_t deadline =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    uint64_t seq = 1;

    Py_BEGIN_ALLOW_THREADS
    while (steady_ns () < deadline) {
        if (!stamp_single_payload (payload, (size_t) msg_size,
                                   (uint32_t) run_id_value, 1, seq)) {
            ok = 0;
            err = EINVAL;
            break;
        }
        int step = spot_publish_one_part (publisher, topic_text, payload,
                                          (size_t) msg_size, ZLINK_DONTWAIT);
        if (step < 0) {
            ok = 0;
            err = zlink_errno ();
            break;
        }
        if (step > 0) {
            struct timespec ts = { 0, 1000000L };
            nanosleep (&ts, NULL);
            continue;
        }
        ++sent;
        ++seq;
    }
    if (ok) {
        for (int retry = 0; retry < 100; ++retry) {
            int step = spot_publish_one_part (
              stop_publisher, topic_text, stop_token.buf,
              (size_t) stop_token.len, ZLINK_SEND_FLAGS_NONE);
            if (step == 0)
                break;
            if (step < 0 || retry == 99) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            struct timespec ts = { 0, 1000000L };
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

    free (payload);
    PyBuffer_Release (&topic);
    PyBuffer_Release (&stop_token);
    return Py_BuildValue ("Ki", sent, ok ? 0 : err);
}

static int
spot_count_topic_matches (spot_count_session_t *session, const char *topic,
                          size_t topic_len)
{
    return session && topic && topic_len == session->topic_len
           && memcmp (topic, session->topic, topic_len) == 0;
}

static void
spot_count_record_payload (spot_count_session_t *session, const void *data,
                           size_t size)
{
    uint64_t sent_ts = 0;
    const unsigned char *p = (const unsigned char *) data;

    if (!session || !data || size < 29 || read_u32_le (p + 0) != 0x5A4C4E4BU
        || read_u32_le (p + 4) != session->run_id
        || read_u32_le (p + 9) != (uint32_t) session->msg_size)
        return;

    if (p[8] == 0) {
        pthread_mutex_lock (&session->lock);
        session->ready_seen = 1;
        pthread_mutex_unlock (&session->lock);
        return;
    }
    if (p[8] != 1)
        return;

    sent_ts = read_u64_le (p + 21);
    pthread_mutex_lock (&session->lock);
    if (session->active && !session->stopped
        && steady_ns () <= session->active_deadline_ns) {
        uint64_t now = now_ns ();
        double sample = sent_ts > 0 && now >= sent_ts ? (double) (now - sent_ts)
                                                     : 0.0;
        session->received += 1;
        if (!latency_samples_add (&session->latencies, sample)) {
            session->failed = 1;
            session->last_errno = ENOMEM;
        }
    }
    pthread_mutex_unlock (&session->lock);
}

static void
spot_count_dispatch_handler (void *spot,
                             const zlink_spot_dispatch_info_t *info,
                             void *userdata)
{
    spot_count_session_t *session = (spot_count_session_t *) userdata;
    if (!session || !info
        || info->event != ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE)
        return;

    for (;;) {
        const zlink_routing_id_t *source_rid = NULL;
        char topic[256];
        size_t topic_len = 0;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        int rc = 0;

        if (zlink_msg_init (&part) != 0) {
            pthread_mutex_lock (&session->lock);
            session->failed = 1;
            session->last_errno = zlink_errno ();
            pthread_mutex_unlock (&session->lock);
            return;
        }

        rc = zlink_spot_subscribe_part (
          spot, &source_rid, topic, sizeof (topic), &topic_len, &part,
          &has_more, ZLINK_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            const int err = zlink_errno ();
            zlink_msg_close (&part);
            if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
                return;
            pthread_mutex_lock (&session->lock);
            session->failed = 1;
            session->last_errno = err;
            pthread_mutex_unlock (&session->lock);
            return;
        }

        if (has_more == ZLINK_PART_FINAL
            && spot_count_topic_matches (session, topic, topic_len)) {
            const void *data = zlink_msg_data (&part);
            const size_t size = zlink_msg_size (&part);
            if (session->stop_token && size == session->stop_token_len
                && memcmp (data, session->stop_token, size) == 0) {
                pthread_mutex_lock (&session->lock);
                session->stopped = 1;
                session->active = 0;
                pthread_mutex_unlock (&session->lock);
                zlink_msg_close (&part);
                return;
            }
            spot_count_record_payload (session, data, size);
        }
        zlink_msg_close (&part);
    }
}

static void
spot_count_capsule_destructor (PyObject *capsule)
{
    spot_count_session_t *session =
      (spot_count_session_t *) PyCapsule_GetPointer (capsule,
                                                     "zlink.spot_count");
    if (!session)
        return;
    free (session->stop_token);
    free (session->latencies.values);
    pthread_mutex_destroy (&session->lock);
    free (session);
}

static spot_count_session_t *
spot_count_from_capsule (PyObject *capsule)
{
    return (spot_count_session_t *) PyCapsule_GetPointer (capsule,
                                                          "zlink.spot_count");
}

static PyObject *
py_spot_count_install (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_buffer topic = { 0 };
    Py_buffer stop_token = { 0 };
    Py_ssize_t msg_size = 0;
    unsigned long long run_id_value = 0;
    spot_count_session_t *session = NULL;
    int rc = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky*y*nK", &handle_value, &topic,
                           &stop_token, &msg_size, &run_id_value))
        return NULL;
    if (topic.len <= 0 || topic.len >= 256
        || memchr (topic.buf, '\0', (size_t) topic.len) || msg_size < 29) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_SetString (PyExc_ValueError, "invalid SPOT count arguments");
        return NULL;
    }

    session = (spot_count_session_t *) calloc (1, sizeof (*session));
    if (!session) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_NoMemory ();
        return NULL;
    }
    session->handle = (void *) (uintptr_t) handle_value;
    session->run_id = (uint32_t) run_id_value;
    session->msg_size = (size_t) msg_size;
    memcpy (session->topic, topic.buf, (size_t) topic.len);
    session->topic[(size_t) topic.len] = '\0';
    session->topic_len = (size_t) topic.len;
    session->stop_token_len = (size_t) stop_token.len;
    if (stop_token.len > 0) {
        session->stop_token = (char *) malloc ((size_t) stop_token.len);
        if (!session->stop_token) {
            free (session);
            PyBuffer_Release (&topic);
            PyBuffer_Release (&stop_token);
            PyErr_NoMemory ();
            return NULL;
        }
        memcpy (session->stop_token, stop_token.buf, (size_t) stop_token.len);
    }
    pthread_mutex_init (&session->lock, NULL);

    Py_BEGIN_ALLOW_THREADS
    rc = zlink_spot_dispatch_event_handler (
      session->handle, spot_count_dispatch_handler, session);
    Py_END_ALLOW_THREADS

    PyBuffer_Release (&topic);
    PyBuffer_Release (&stop_token);
    if (rc != 0) {
        const int err = zlink_errno ();
        free (session->stop_token);
        pthread_mutex_destroy (&session->lock);
        free (session);
        Py_INCREF (Py_None);
        return Py_BuildValue ("Ni", Py_None, err);
    }

    PyObject *capsule =
      PyCapsule_New (session, "zlink.spot_count",
                     spot_count_capsule_destructor);
    if (!capsule) {
        free (session->stop_token);
        pthread_mutex_destroy (&session->lock);
        free (session);
        return NULL;
    }
    return Py_BuildValue ("Ni", capsule, 0);
}

static PyObject *
py_spot_count_start (PyObject *self, PyObject *args)
{
    PyObject *capsule = NULL;
    int duration_s = 0;
    spot_count_session_t *session = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "Oi", &capsule, &duration_s))
        return NULL;
    session = spot_count_from_capsule (capsule);
    if (!session)
        return NULL;
    if (duration_s <= 0) {
        PyErr_SetString (PyExc_ValueError, "duration must be positive");
        return NULL;
    }

    pthread_mutex_lock (&session->lock);
    session->active = 1;
    session->stopped = 0;
    session->failed = 0;
    session->last_errno = 0;
    session->received = 0;
    session->latencies.count = 0;
    session->latencies.sum = 0.0;
    session->active_deadline_ns =
      steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    pthread_mutex_unlock (&session->lock);

    Py_RETURN_NONE;
}

static PyObject *
py_spot_count_stats (PyObject *self, PyObject *args)
{
    PyObject *capsule = NULL;
    int duration_s = 1;
    int ready = 0;
    int stopped = 0;
    int failed = 0;
    int err = 0;
    unsigned long long received = 0;
    double throughput = 0.0;
    double bandwidth = 0.0;
    double mean_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    latency_samples_t snapshot = { 0 };
    spot_count_session_t *session = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "O|i", &capsule, &duration_s))
        return NULL;
    session = spot_count_from_capsule (capsule);
    if (!session)
        return NULL;
    if (duration_s <= 0)
        duration_s = 1;

    pthread_mutex_lock (&session->lock);
    ready = session->ready_seen;
    stopped = session->stopped;
    failed = session->failed;
    err = session->last_errno;
    received = session->received;
    snapshot.count = session->latencies.count;
    snapshot.capacity = session->latencies.count;
    snapshot.sum = session->latencies.sum;
    if (snapshot.count > 0) {
        snapshot.values =
          (double *) malloc (snapshot.count * sizeof (double));
        if (snapshot.values)
            memcpy (snapshot.values, session->latencies.values,
                    snapshot.count * sizeof (double));
        else {
            failed = 1;
            err = ENOMEM;
        }
    }
    pthread_mutex_unlock (&session->lock);

    if (snapshot.values && snapshot.count > 0) {
        qsort (snapshot.values, snapshot.count, sizeof (double),
               compare_double);
        mean_ms = (snapshot.sum / (double) snapshot.count) / 1000000.0;
        p95_ms = latency_percentile_sorted (&snapshot, 0.95) / 1000000.0;
        p99_ms = latency_percentile_sorted (&snapshot, 0.99) / 1000000.0;
    }
    throughput = (double) received / (double) duration_s;
    bandwidth = ((double) received * (double) session->msg_size)
                / (double) duration_s / 1000000.0;
    free (snapshot.values);

    return Py_BuildValue ("iiiKdddddi", ready, stopped, failed, received,
                          throughput, bandwidth, mean_ms, p95_ms, p99_ms, err);
}

static void
close_received_parts (received_parts_t *received)
{
    if (!received || !received->parts)
        return;
    for (Py_ssize_t i = 0; i < received->count; ++i)
        zlink_msg_close (&received->parts[i]);
    free (received->parts);
    received->parts = NULL;
    received->count = 0;
    received->capacity = 0;
}

static int
append_received_part (received_parts_t *received, zlink_msg_t *part)
{
    if (received->count == received->capacity) {
        Py_ssize_t next_capacity =
          received->capacity == 0 ? 4 : received->capacity * 2;
        zlink_msg_t *next =
          realloc (received->parts, (size_t) next_capacity * sizeof (zlink_msg_t));
        if (!next)
            return -1;
        received->parts = next;
        received->capacity = next_capacity;
    }
    received->parts[received->count++] = *part;
    return 0;
}

static PyObject *
py_send_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    PyObject *payload = NULL;
    int flags = 0;
    prepared_parts_t prepared;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;
    Py_ssize_t close_from = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "KOi", &handle_value, &payload, &flags))
        return NULL;
    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_send_part (handle, &prepared.parts[i],
                              (zlink_send_flags_t) flags,
                              part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    return result_tuple (rc, err);
}

static int
copy_routing_id (Py_buffer *view, zlink_routing_id_t *rid)
{
    if (view->len <= 0 || view->len > 255) {
        PyErr_SetString (PyExc_ValueError,
                         "routing_id length must be between 1 and 255");
        return -1;
    }
    rid->size = (uint8_t) view->len;
    memcpy (rid->data, view->buf, (size_t) view->len);
    return 0;
}

static PyObject *
py_send_parts_rid (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    PyObject *routing_id_obj = NULL;
    PyObject *payload = NULL;
    int flags = 0;
    Py_buffer rid_view = { 0 };
    zlink_routing_id_t rid;
    prepared_parts_t prepared;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;
    Py_ssize_t close_from = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "KOOi", &handle_value, &routing_id_obj,
                           &payload, &flags))
        return NULL;
    if (PyObject_GetBuffer (routing_id_obj, &rid_view, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (copy_routing_id (&rid_view, &rid) != 0) {
        PyBuffer_Release (&rid_view);
        return NULL;
    }
    PyBuffer_Release (&rid_view);
    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_send_part_rid (handle, &rid, &prepared.parts[i],
                                  (zlink_send_flags_t) flags,
                                  part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    return result_tuple (rc, err);
}

static PyObject *
py_publish_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    const char *topic = NULL;
    Py_ssize_t topic_len = 0;
    PyObject *payload = NULL;
    int flags = 0;
    prepared_parts_t prepared;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;
    Py_ssize_t close_from = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky#Oi", &handle_value, &topic, &topic_len,
                           &payload, &flags))
        return NULL;
    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    void *handle = (void *) (uintptr_t) handle_value;
    const int release_gil = (flags & ZLINK_DONTWAIT) == 0;
    PyThreadState *_save = NULL;
    if (release_gil)
        _save = PyEval_SaveThread ();
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_publish_part (handle, topic, &prepared.parts[i],
                                 (zlink_send_flags_t) flags,
                                 part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    if (release_gil)
        PyEval_RestoreThread (_save);

    release_prepared_parts (&prepared, close_from);
    return result_tuple (rc, err);
}

static PyObject *
py_spot_publish_submit_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    const char *topic = NULL;
    Py_ssize_t topic_len = 0;
    PyObject *payload = NULL;
    int flags = 0;
    prepared_parts_t prepared;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;
    Py_ssize_t close_from = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky#Oi", &handle_value, &topic, &topic_len,
                           &payload, &flags))
        return NULL;
    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_spot_publish_part (handle, topic, &prepared.parts[i],
                                      (zlink_send_flags_t) flags,
                                      part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    return result_tuple (rc, err);
}

static PyObject *
py_spot_send_channel_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    const char *channel = NULL;
    Py_ssize_t channel_len = 0;
    PyObject *payload = NULL;
    int flags = 0;
    prepared_parts_t prepared;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;
    Py_ssize_t close_from = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky#Oi", &handle_value, &channel, &channel_len,
                           &payload, &flags))
        return NULL;
    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_spot_send_channel_part (handle, channel, &prepared.parts[i],
                                           (zlink_send_flags_t) flags,
                                           part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    return result_tuple (rc, err);
}

static PyObject *
py_spot_send_spot_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    PyObject *node_rid_obj = NULL;
    PyObject *spot_rid_obj = NULL;
    PyObject *payload = NULL;
    int flags = 0;
    Py_buffer node_view = { 0 };
    Py_buffer spot_view = { 0 };
    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    prepared_parts_t prepared;
    int rc = ZLINK_SUBMIT_OK;
    int err = 0;
    Py_ssize_t close_from = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "KOOOi", &handle_value, &node_rid_obj,
                           &spot_rid_obj, &payload, &flags))
        return NULL;
    if (PyObject_GetBuffer (node_rid_obj, &node_view, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (copy_routing_id (&node_view, &node_rid) != 0) {
        PyBuffer_Release (&node_view);
        return NULL;
    }
    PyBuffer_Release (&node_view);
    if (PyObject_GetBuffer (spot_rid_obj, &spot_view, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (copy_routing_id (&spot_view, &spot_rid) != 0) {
        PyBuffer_Release (&spot_view);
        return NULL;
    }
    PyBuffer_Release (&spot_view);
    if (prepare_parts (payload, &prepared) != 0)
        return NULL;
    close_from = prepared.count;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    for (Py_ssize_t i = 0; i < prepared.count; ++i) {
        rc = zlink_spot_send_spot_part (handle, &node_rid, &spot_rid,
                                        &prepared.parts[i],
                                        (zlink_send_flags_t) flags,
                                        part_flag (i, prepared.count));
        if (rc != ZLINK_SUBMIT_OK) {
            err = zlink_errno ();
            for (Py_ssize_t j = i; j < prepared.count; ++j)
                zlink_msg_close (&prepared.parts[j]);
            break;
        }
    }
    Py_END_ALLOW_THREADS

    release_prepared_parts (&prepared, close_from);
    return result_tuple (rc, err);
}

static int
stream_echo_build_frame (zlink_msg_t *packet, zlink_msg_t *header,
                         zlink_msg_t *body)
{
    size_t header_size = zlink_msg_size (header);
    size_t body_size = zlink_msg_size (body);
    size_t total_size = 6 + header_size + body_size;
    unsigned char *dst = NULL;

    if (header_size > UINT16_MAX || body_size > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (zlink_msg_init_size (packet, total_size) != ZLINK_CONFIG_OK)
        return -1;

    dst = (unsigned char *) zlink_msg_data (packet);
    dst[0] = (unsigned char) ((header_size >> 8) & 0xff);
    dst[1] = (unsigned char) (header_size & 0xff);
    dst[2] = (unsigned char) ((body_size >> 24) & 0xff);
    dst[3] = (unsigned char) ((body_size >> 16) & 0xff);
    dst[4] = (unsigned char) ((body_size >> 8) & 0xff);
    dst[5] = (unsigned char) (body_size & 0xff);
    if (header_size > 0)
        memcpy (dst + 6, zlink_msg_data (header), header_size);
    if (body_size > 0)
        memcpy (dst + 6 + header_size, zlink_msg_data (body), body_size);
    return 0;
}

static int
stream_echo_try_send (stream_echo_session_t *session,
                      const zlink_routing_id_t *routing_id, zlink_msg_t *packet)
{
    int rc = ZLINK_SUBMIT_OK;

    pthread_mutex_lock (&session->send_lock);
    rc = zlink_send_part_rid (session->handle, routing_id, packet, ZLINK_DONTWAIT,
                              ZLINK_PART_FINAL);
    pthread_mutex_unlock (&session->send_lock);
    if (rc == ZLINK_SUBMIT_OK)
        return 0;
    session->last_errno = zlink_errno ();
    if (session->last_errno == EAGAIN)
        return 1;
    session->failed = 1;
    session->stop_requested = 1;
    return -1;
}

static int
stream_echo_enqueue (stream_echo_session_t *session,
                     const zlink_routing_id_t *routing_id, zlink_msg_t *packet)
{
    stream_echo_item_t *item =
      (stream_echo_item_t *) calloc (1, sizeof (stream_echo_item_t));
    if (!item) {
        session->last_errno = ENOMEM;
        session->failed = 1;
        session->stop_requested = 1;
        return -1;
    }
    item->routing_id = *routing_id;
    if (zlink_msg_init (&item->msg) != ZLINK_CONFIG_OK
        || zlink_msg_move (&item->msg, packet) != ZLINK_CONFIG_OK) {
        free (item);
        session->last_errno = zlink_errno ();
        session->failed = 1;
        session->stop_requested = 1;
        return -1;
    }

    pthread_mutex_lock (&session->queue_lock);
    if (session->tail)
        session->tail->next = item;
    else
        session->head = item;
    session->tail = item;
    session->pending_count++;
    pthread_mutex_unlock (&session->queue_lock);
    return 0;
}

static int
stream_echo_body_is_stop (stream_echo_session_t *session, zlink_msg_t *body)
{
    return session->stop_token && session->stop_token_len > 0
           && zlink_msg_size (body) == session->stop_token_len
           && memcmp (zlink_msg_data (body), session->stop_token,
                      session->stop_token_len)
                == 0;
}

static void
stream_echo_packet_handler (void *stream, const zlink_routing_id_t *routing_id,
                            zlink_msg_t *header, zlink_msg_t *body,
                            void *userdata)
{
    stream_echo_session_t *session = (stream_echo_session_t *) userdata;
    zlink_msg_t packet;
    int send_result = 0;

    if (!session || !stream || !routing_id || !header || !body) {
        if (header)
            zlink_msg_close (header);
        if (body)
            zlink_msg_close (body);
        return;
    }

    if (stream_echo_body_is_stop (session, body)) {
        session->stop_requested = 1;
        zlink_msg_close (header);
        zlink_msg_close (body);
        return;
    }

    memset (&packet, 0, sizeof (packet));
    session->recv_count++;
    if (stream_echo_build_frame (&packet, header, body) != 0) {
        session->last_errno = errno ? errno : zlink_errno ();
        session->failed = 1;
        session->stop_requested = 1;
        zlink_msg_close (header);
        zlink_msg_close (body);
        return;
    }

    send_result = stream_echo_try_send (session, routing_id, &packet);
    if (send_result == 0) {
        session->send_count++;
        zlink_msg_close (&packet);
    } else if (send_result > 0) {
        if (stream_echo_enqueue (session, routing_id, &packet) != 0)
            zlink_msg_close (&packet);
    } else {
        zlink_msg_close (&packet);
    }

    zlink_msg_close (header);
    zlink_msg_close (body);
}

static void
stream_echo_free_queue (stream_echo_session_t *session)
{
    stream_echo_item_t *item = NULL;
    stream_echo_item_t *next = NULL;

    if (!session)
        return;
    item = session->head;
    while (item) {
        next = item->next;
        zlink_msg_close (&item->msg);
        free (item);
        item = next;
    }
    session->head = NULL;
    session->tail = NULL;
    session->pending_count = 0;
}

static void
stream_echo_capsule_destructor (PyObject *capsule)
{
    stream_echo_session_t *session =
      (stream_echo_session_t *) PyCapsule_GetPointer (capsule,
                                                      "zlink.StreamEchoSession");
    if (!session)
        return;
    if (session->handle)
        zlink_stream_packet_handler (session->handle, NULL, NULL);
    pthread_mutex_lock (&session->queue_lock);
    stream_echo_free_queue (session);
    pthread_mutex_unlock (&session->queue_lock);
    pthread_mutex_destroy (&session->queue_lock);
    pthread_mutex_destroy (&session->send_lock);
    free (session->stop_token);
    free (session);
}

static PyObject *
py_stream_echo_install (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    const char *stop_token = NULL;
    Py_ssize_t stop_token_len = 0;
    stream_echo_session_t *session = NULL;
    int rc = ZLINK_HANDLER_OK;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky#", &handle_value, &stop_token,
                           &stop_token_len))
        return NULL;

    session = (stream_echo_session_t *) calloc (1, sizeof (*session));
    if (!session) {
        PyErr_NoMemory ();
        return NULL;
    }
    session->handle = (void *) (uintptr_t) handle_value;
    if (pthread_mutex_init (&session->send_lock, NULL) != 0
        || pthread_mutex_init (&session->queue_lock, NULL) != 0) {
        free (session);
        PyErr_SetFromErrno (PyExc_OSError);
        return NULL;
    }
    session->stop_token = (char *) malloc ((size_t) stop_token_len + 1);
    if (!session->stop_token) {
        pthread_mutex_destroy (&session->queue_lock);
        pthread_mutex_destroy (&session->send_lock);
        free (session);
        PyErr_NoMemory ();
        return NULL;
    }
    memcpy (session->stop_token, stop_token, (size_t) stop_token_len);
    session->stop_token[stop_token_len] = '\0';
    session->stop_token_len = (size_t) stop_token_len;

    Py_BEGIN_ALLOW_THREADS
    rc = zlink_stream_packet_handler (session->handle,
                                      stream_echo_packet_handler, session);
    Py_END_ALLOW_THREADS
    if (rc != ZLINK_HANDLER_OK) {
        int err = zlink_errno ();
        pthread_mutex_destroy (&session->queue_lock);
        pthread_mutex_destroy (&session->send_lock);
        free (session->stop_token);
        free (session);
        errno = err;
        PyErr_SetFromErrno (PyExc_OSError);
        return NULL;
    }

    return PyCapsule_New (session, "zlink.StreamEchoSession",
                          stream_echo_capsule_destructor);
}

static PyObject *
py_stream_echo_drain (PyObject *self, PyObject *args)
{
    PyObject *capsule = NULL;
    stream_echo_session_t *session = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "O", &capsule))
        return NULL;
    session = (stream_echo_session_t *) PyCapsule_GetPointer (
      capsule, "zlink.StreamEchoSession");
    if (!session)
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    while (!session->stop_requested) {
        stream_echo_item_t *item = NULL;
        int send_result = 0;

        pthread_mutex_lock (&session->queue_lock);
        item = session->head;
        if (item) {
            session->head = item->next;
            if (!session->head)
                session->tail = NULL;
            item->next = NULL;
        }
        pthread_mutex_unlock (&session->queue_lock);
        if (!item)
            break;

        Py_BEGIN_ALLOW_THREADS
        send_result = stream_echo_try_send (session, &item->routing_id, &item->msg);
        Py_END_ALLOW_THREADS
        if (send_result == 0) {
            session->send_count++;
            if (session->pending_count > 0)
                session->pending_count--;
            zlink_msg_close (&item->msg);
            free (item);
            continue;
        }

        pthread_mutex_lock (&session->queue_lock);
        item->next = session->head;
        session->head = item;
        if (!session->tail)
            session->tail = item;
        pthread_mutex_unlock (&session->queue_lock);
        break;
    }
    Py_END_ALLOW_THREADS

    PyObject *stopped =
      PyBool_FromLong (session->stop_requested || session->failed);
    PyObject *pending = PyLong_FromUnsignedLongLong (session->pending_count);
    if (!stopped || !pending) {
        Py_XDECREF (stopped);
        Py_XDECREF (pending);
        return NULL;
    }
    PyObject *result = PyTuple_Pack (2, stopped, pending);
    Py_DECREF (stopped);
    Py_DECREF (pending);
    return result;
}

static PyObject *
py_stream_echo_stats (PyObject *self, PyObject *args)
{
    PyObject *capsule = NULL;
    stream_echo_session_t *session = NULL;

    (void) self;
    if (!PyArg_ParseTuple (args, "O", &capsule))
        return NULL;
    session = (stream_echo_session_t *) PyCapsule_GetPointer (
      capsule, "zlink.StreamEchoSession");
    if (!session)
        return NULL;
    return Py_BuildValue ("KKKii", session->recv_count, session->send_count,
                          session->pending_count, session->failed,
                          session->last_errno);
}

static PyObject *
build_recv_result (int rc, int err, const zlink_routing_id_t *routing_id,
                   received_parts_t *received)
{
    PyObject *routing_obj = Py_None;
    PyObject *parts_obj = Py_None;
    PyObject *result = NULL;

    if (rc != ZLINK_RECV_OK) {
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNN", rc, err, Py_None, Py_None);
    }

    if (routing_id && routing_id->size > 0) {
        routing_obj =
          PyBytes_FromStringAndSize ((const char *) routing_id->data,
                                     (Py_ssize_t) routing_id->size);
        if (!routing_obj)
            return NULL;
    } else {
        Py_INCREF (Py_None);
    }

    parts_obj = PyTuple_New (received->count);
    if (!parts_obj) {
        Py_DECREF (routing_obj);
        return NULL;
    }

    for (Py_ssize_t i = 0; i < received->count; ++i) {
        void *data = zlink_msg_data (&received->parts[i]);
        size_t size = zlink_msg_size (&received->parts[i]);
        PyObject *part = PyBytes_FromStringAndSize ((const char *) data,
                                                    (Py_ssize_t) size);
        if (!part) {
            Py_DECREF (routing_obj);
            Py_DECREF (parts_obj);
            return NULL;
        }
        PyTuple_SET_ITEM (parts_obj, i, part);
    }

    result = Py_BuildValue ("iiNN", rc, err, routing_obj, parts_obj);
    return result;
}

static PyObject *
py_recv_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t routing_copy;
    int has_routing = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&routing_copy, 0, sizeof (routing_copy));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    while (1) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_recv_part (handle, &source_rid, &part, &has_more,
                              recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0 && source_rid && source_rid->size > 0) {
            routing_copy = *source_rid;
            has_routing = 1;
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    Py_END_ALLOW_THREADS

    PyObject *result = build_recv_result (
      rc, err, has_routing ? &routing_copy : NULL, &received);
    close_received_parts (&received);
    return result;
}

static PyObject *
py_recv_owner (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t routing_copy;
    int has_routing = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&routing_copy, 0, sizeof (routing_copy));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    const int release_gil = (flags & ZLINK_DONTWAIT) == 0;
    PyThreadState *_save = NULL;
    if (release_gil)
        _save = PyEval_SaveThread ();
    while (1) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_recv_part (handle, &source_rid, &part, &has_more,
                              recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0 && source_rid && source_rid->size > 0) {
            routing_copy = *source_rid;
            has_routing = 1;
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    if (release_gil)
        PyEval_RestoreThread (_save);

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        if ((flags & ZLINK_DONTWAIT) && is_recv_no_data_result (rc))
            Py_RETURN_FALSE;
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNN", rc, err, Py_None, Py_None);
    }

    PyObject *routing_obj = Py_None;
    PyObject *owner_obj = build_native_parts_owner (&received);
    if (!owner_obj) {
        close_received_parts (&received);
        return NULL;
    }
    if (has_routing) {
        routing_obj =
          PyBytes_FromStringAndSize ((const char *) routing_copy.data,
                                     (Py_ssize_t) routing_copy.size);
        if (!routing_obj) {
            Py_DECREF (owner_obj);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }
    return Py_BuildValue ("iiNN", rc, err, routing_obj, owner_obj);
}

static PyObject *
py_subscribe_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t routing_copy;
    int has_routing = 0;
    char topic[256];
    size_t topic_len = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&routing_copy, 0, sizeof (routing_copy));
    memset (topic, 0, sizeof (topic));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    while (1) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        char next_topic[256];
        size_t next_topic_len = 0;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_subscribe_part (handle, &source_rid, next_topic,
                                   sizeof (next_topic), &next_topic_len, &part,
                                   &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0) {
            if (source_rid && source_rid->size > 0) {
                routing_copy = *source_rid;
                has_routing = 1;
            }
            topic_len = next_topic_len < sizeof (topic) ? next_topic_len
                                                        : sizeof (topic);
            memcpy (topic, next_topic, topic_len);
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    Py_END_ALLOW_THREADS

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        if ((flags & ZLINK_DONTWAIT) && is_recv_no_data_result (rc))
            Py_RETURN_FALSE;
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNNN", rc, err, Py_None, Py_None, Py_None);
    }

    PyObject *routing_obj = Py_None;
    PyObject *topic_obj = PyBytes_FromStringAndSize (topic, (Py_ssize_t) topic_len);
    PyObject *parts_obj = PyTuple_New (received.count);
    if (!topic_obj || !parts_obj) {
        Py_XDECREF (topic_obj);
        Py_XDECREF (parts_obj);
        close_received_parts (&received);
        return NULL;
    }
    if (has_routing) {
        routing_obj =
          PyBytes_FromStringAndSize ((const char *) routing_copy.data,
                                     (Py_ssize_t) routing_copy.size);
        if (!routing_obj) {
            Py_DECREF (topic_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }

    for (Py_ssize_t i = 0; i < received.count; ++i) {
        void *data = zlink_msg_data (&received.parts[i]);
        size_t size = zlink_msg_size (&received.parts[i]);
        PyObject *part = PyBytes_FromStringAndSize ((const char *) data,
                                                    (Py_ssize_t) size);
        if (!part) {
            Py_DECREF (routing_obj);
            Py_DECREF (topic_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
        PyTuple_SET_ITEM (parts_obj, i, part);
    }

    PyObject *result =
      Py_BuildValue ("iiNNN", rc, err, routing_obj, topic_obj, parts_obj);
    close_received_parts (&received);
    return result;
}

static PyObject *
py_subscribe_owner (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t routing_copy;
    int has_routing = 0;
    char topic[256];
    size_t topic_len = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&routing_copy, 0, sizeof (routing_copy));
    memset (topic, 0, sizeof (topic));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    const int release_gil = (flags & ZLINK_DONTWAIT) == 0;
    PyThreadState *_save = NULL;
    if (release_gil)
        _save = PyEval_SaveThread ();
    while (1) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        char next_topic[256];
        size_t next_topic_len = 0;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_subscribe_part (handle, &source_rid, next_topic,
                                   sizeof (next_topic), &next_topic_len, &part,
                                   &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0) {
            if (source_rid && source_rid->size > 0) {
                routing_copy = *source_rid;
                has_routing = 1;
            }
            topic_len = next_topic_len < sizeof (topic) ? next_topic_len
                                                        : sizeof (topic);
            memcpy (topic, next_topic, topic_len);
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    if (release_gil)
        PyEval_RestoreThread (_save);

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        if ((flags & ZLINK_DONTWAIT) && is_recv_no_data_result (rc))
            Py_RETURN_FALSE;
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNNN", rc, err, Py_None, Py_None, Py_None);
    }

    PyObject *routing_obj = Py_None;
    PyObject *topic_obj = PyBytes_FromStringAndSize (topic, (Py_ssize_t) topic_len);
    PyObject *owner_obj = build_native_parts_owner (&received);
    if (!topic_obj || !owner_obj) {
        Py_XDECREF (topic_obj);
        Py_XDECREF (owner_obj);
        close_received_parts (&received);
        return NULL;
    }
    if (has_routing) {
        routing_obj =
          PyBytes_FromStringAndSize ((const char *) routing_copy.data,
                                     (Py_ssize_t) routing_copy.size);
        if (!routing_obj) {
            Py_DECREF (topic_obj);
            Py_DECREF (owner_obj);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }

    return Py_BuildValue ("iiNNN", rc, err, routing_obj, topic_obj, owner_obj);
}

static PyObject *
py_router_recv_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t peer_copy;
    zlink_routing_id_t spot_copy;
    int has_peer = 0;
    int has_spot = 0;
    uint64_t request_seq = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&peer_copy, 0, sizeof (peer_copy));
    memset (&spot_copy, 0, sizeof (spot_copy));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    while (1) {
        const zlink_routing_id_t *peer_rid = NULL;
        const zlink_routing_id_t *spot_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_router_recv_part (handle, &peer_rid, &spot_rid, &request_seq,
                                     &part, &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0) {
            if (peer_rid && peer_rid->size > 0) {
                peer_copy = *peer_rid;
                has_peer = 1;
            }
            if (spot_rid && spot_rid->size > 0) {
                spot_copy = *spot_rid;
                has_spot = 1;
            }
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    Py_END_ALLOW_THREADS

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        if ((flags & ZLINK_DONTWAIT) && is_recv_no_data_result (rc))
            Py_RETURN_FALSE;
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNNKN", rc, err, Py_None, Py_None,
                              (unsigned long long) 0, Py_None);
    }

    PyObject *peer_obj = Py_None;
    PyObject *spot_obj = Py_None;
    PyObject *parts_obj = PyTuple_New (received.count);
    if (!parts_obj) {
        close_received_parts (&received);
        return NULL;
    }
    if (has_peer) {
        peer_obj = PyBytes_FromStringAndSize ((const char *) peer_copy.data,
                                              (Py_ssize_t) peer_copy.size);
        if (!peer_obj) {
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }
    if (has_spot) {
        spot_obj = PyBytes_FromStringAndSize ((const char *) spot_copy.data,
                                              (Py_ssize_t) spot_copy.size);
        if (!spot_obj) {
            Py_DECREF (peer_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }

    for (Py_ssize_t i = 0; i < received.count; ++i) {
        void *data = zlink_msg_data (&received.parts[i]);
        size_t size = zlink_msg_size (&received.parts[i]);
        PyObject *part = PyBytes_FromStringAndSize ((const char *) data,
                                                    (Py_ssize_t) size);
        if (!part) {
            Py_DECREF (peer_obj);
            Py_DECREF (spot_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
        PyTuple_SET_ITEM (parts_obj, i, part);
    }

    PyObject *result =
      Py_BuildValue ("iiNNKN", rc, err, peer_obj, spot_obj,
                     (unsigned long long) request_seq, parts_obj);
    close_received_parts (&received);
    return result;
}

static PyObject *
py_router_recv_owner (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t peer_copy;
    zlink_routing_id_t spot_copy;
    int has_peer = 0;
    int has_spot = 0;
    uint64_t request_seq = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&peer_copy, 0, sizeof (peer_copy));
    memset (&spot_copy, 0, sizeof (spot_copy));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    const int release_gil = (flags & ZLINK_DONTWAIT) == 0;
    PyThreadState *_save = NULL;
    if (release_gil)
        _save = PyEval_SaveThread ();
    while (1) {
        const zlink_routing_id_t *peer_rid = NULL;
        const zlink_routing_id_t *spot_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_router_recv_part (handle, &peer_rid, &spot_rid, &request_seq,
                                     &part, &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0) {
            if (peer_rid && peer_rid->size > 0) {
                peer_copy = *peer_rid;
                has_peer = 1;
            }
            if (spot_rid && spot_rid->size > 0) {
                spot_copy = *spot_rid;
                has_spot = 1;
            }
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    if (release_gil)
        PyEval_RestoreThread (_save);

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        if ((flags & ZLINK_DONTWAIT) && is_recv_no_data_result (rc))
            Py_RETURN_FALSE;
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNNKN", rc, err, Py_None, Py_None,
                              (unsigned long long) 0, Py_None);
    }

    PyObject *peer_obj = Py_None;
    PyObject *spot_obj = Py_None;
    PyObject *owner_obj = build_native_parts_owner (&received);
    if (!owner_obj) {
        close_received_parts (&received);
        return NULL;
    }
    if (has_peer) {
        peer_obj = PyBytes_FromStringAndSize ((const char *) peer_copy.data,
                                              (Py_ssize_t) peer_copy.size);
        if (!peer_obj) {
            Py_DECREF (owner_obj);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }
    if (has_spot) {
        spot_obj = PyBytes_FromStringAndSize ((const char *) spot_copy.data,
                                              (Py_ssize_t) spot_copy.size);
        if (!spot_obj) {
            Py_DECREF (peer_obj);
            Py_DECREF (owner_obj);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }

    return Py_BuildValue ("iiNNKN", rc, err, peer_obj, spot_obj,
                          (unsigned long long) request_seq, owner_obj);
}

static PyObject *
py_spot_subscribe_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t routing_copy;
    int has_routing = 0;
    char topic[256];
    size_t topic_len = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&routing_copy, 0, sizeof (routing_copy));
    memset (topic, 0, sizeof (topic));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    while (1) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        char next_topic[256];
        size_t next_topic_len = 0;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        rc = zlink_spot_subscribe_part (handle, &source_rid, next_topic,
                                        sizeof (next_topic), &next_topic_len,
                                        &part, &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            break;
        }
        if (received.count == 0) {
            if (source_rid && source_rid->size > 0) {
                routing_copy = *source_rid;
                has_routing = 1;
            }
            topic_len = next_topic_len < sizeof (topic) ? next_topic_len
                                                        : sizeof (topic);
            memcpy (topic, next_topic, topic_len);
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    Py_END_ALLOW_THREADS

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        if ((flags & ZLINK_DONTWAIT) && is_recv_no_data_result (rc))
            Py_RETURN_FALSE;
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNNN", rc, err, Py_None, Py_None, Py_None);
    }

    PyObject *routing_obj = Py_None;
    PyObject *topic_obj = PyBytes_FromStringAndSize (topic, (Py_ssize_t) topic_len);
    PyObject *parts_obj = PyTuple_New (received.count);
    if (!topic_obj || !parts_obj) {
        Py_XDECREF (topic_obj);
        Py_XDECREF (parts_obj);
        close_received_parts (&received);
        return NULL;
    }
    if (has_routing) {
        routing_obj =
          PyBytes_FromStringAndSize ((const char *) routing_copy.data,
                                     (Py_ssize_t) routing_copy.size);
        if (!routing_obj) {
            Py_DECREF (topic_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }

    for (Py_ssize_t i = 0; i < received.count; ++i) {
        void *data = zlink_msg_data (&received.parts[i]);
        size_t size = zlink_msg_size (&received.parts[i]);
        PyObject *part = PyBytes_FromStringAndSize ((const char *) data,
                                                    (Py_ssize_t) size);
        if (!part) {
            Py_DECREF (routing_obj);
            Py_DECREF (topic_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
        PyTuple_SET_ITEM (parts_obj, i, part);
    }

    PyObject *result =
      Py_BuildValue ("iiNNN", rc, err, routing_obj, topic_obj, parts_obj);
    close_received_parts (&received);
    return result;
}

static PyObject *
py_spot_recv_parts (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    int flags = 0;
    int rc = ZLINK_RECV_OK;
    int err = 0;
    zlink_routing_id_t peer_copy;
    zlink_routing_id_t spot_copy;
    int has_peer = 0;
    int has_spot = 0;
    uint64_t request_seq = 0;
    received_parts_t received = { 0 };

    (void) self;
    memset (&peer_copy, 0, sizeof (peer_copy));
    memset (&spot_copy, 0, sizeof (spot_copy));
    if (!PyArg_ParseTuple (args, "Ki", &handle_value, &flags))
        return NULL;

    void *handle = (void *) (uintptr_t) handle_value;
    Py_BEGIN_ALLOW_THREADS
    while (1) {
        const zlink_routing_id_t *peer_rid = NULL;
        const zlink_routing_id_t *spot_rid = NULL;
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_flags_t recv_flags =
          received.count == 0 ? (zlink_recv_flags_t) flags : ZLINK_DONTWAIT;

        if (zlink_msg_init (&part) != ZLINK_CONFIG_OK) {
            err = zlink_errno ();
            if (err == 0)
                err = ENOMEM;
            rc = ZLINK_RECV_INTERNAL_ERROR;
            break;
        }
        rc = zlink_spot_recv_part (handle, &peer_rid, &spot_rid, &request_seq,
                                   &part, &has_more, recv_flags);
        if (rc != ZLINK_RECV_OK) {
            err = zlink_errno ();
            zlink_msg_close (&part);
            break;
        }
        if (received.count == 0) {
            if (peer_rid && peer_rid->size > 0) {
                peer_copy = *peer_rid;
                has_peer = 1;
            }
            if (spot_rid && spot_rid->size > 0) {
                spot_copy = *spot_rid;
                has_spot = 1;
            }
        }
        if (append_received_part (&received, &part) != 0) {
            zlink_msg_close (&part);
            rc = ZLINK_RECV_INTERNAL_ERROR;
            err = ENOMEM;
            break;
        }
        if (has_more == ZLINK_PART_FINAL)
            break;
    }
    Py_END_ALLOW_THREADS

    if (rc != ZLINK_RECV_OK) {
        close_received_parts (&received);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        Py_INCREF (Py_None);
        return Py_BuildValue ("iiNNKN", rc, err, Py_None, Py_None,
                              (unsigned long long) 0, Py_None);
    }

    PyObject *peer_obj = Py_None;
    PyObject *spot_obj = Py_None;
    PyObject *parts_obj = PyTuple_New (received.count);
    if (!parts_obj) {
        close_received_parts (&received);
        return NULL;
    }
    if (has_peer) {
        peer_obj = PyBytes_FromStringAndSize ((const char *) peer_copy.data,
                                              (Py_ssize_t) peer_copy.size);
        if (!peer_obj) {
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }
    if (has_spot) {
        spot_obj = PyBytes_FromStringAndSize ((const char *) spot_copy.data,
                                              (Py_ssize_t) spot_copy.size);
        if (!spot_obj) {
            Py_DECREF (peer_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
    } else {
        Py_INCREF (Py_None);
    }

    for (Py_ssize_t i = 0; i < received.count; ++i) {
        void *data = zlink_msg_data (&received.parts[i]);
        size_t size = zlink_msg_size (&received.parts[i]);
        PyObject *part = PyBytes_FromStringAndSize ((const char *) data,
                                                    (Py_ssize_t) size);
        if (!part) {
            Py_DECREF (peer_obj);
            Py_DECREF (spot_obj);
            Py_DECREF (parts_obj);
            close_received_parts (&received);
            return NULL;
        }
        PyTuple_SET_ITEM (parts_obj, i, part);
    }

    PyObject *result =
      Py_BuildValue ("iiNNKN", rc, err, peer_obj, spot_obj,
                     (unsigned long long) request_seq, parts_obj);
    close_received_parts (&received);
    return result;
}

static PyMethodDef zlink_native_methods[] = {
    { "socket_send_op", py_socket_send_op, METH_O,
      "Create a native simple socket send builder." },
    { "routed_send_op", py_routed_send_op, METH_VARARGS,
      "Create a native routed socket send builder." },
    { "publisher_send_op", py_publisher_send_op, METH_VARARGS,
      "Create a native publisher send builder." },
    { "send_parts", py_send_parts, METH_VARARGS,
      "Submit multipart payload parts through zlink_send_part." },
    { "send_parts_rid", py_send_parts_rid, METH_VARARGS,
      "Submit routed multipart payload parts through zlink_send_part_rid." },
    { "publish_parts", py_publish_parts, METH_VARARGS,
      "Submit topic multipart payload parts through zlink_publish_part." },
    { "spot_publish_submit_parts", py_spot_publish_submit_parts, METH_VARARGS,
      "Submit spot topic multipart payload parts through zlink_spot_publish_part." },
    { "spot_send_channel_parts", py_spot_send_channel_parts, METH_VARARGS,
      "Submit spot channel multipart payload parts through zlink_spot_send_channel_part." },
    { "spot_send_spot_parts", py_spot_send_spot_parts, METH_VARARGS,
      "Submit spot-to-spot multipart payload parts through zlink_spot_send_spot_part." },
    { "recv_parts", py_recv_parts, METH_VARARGS,
      "Receive multipart payload parts through zlink_recv_part." },
    { "recv_owner", py_recv_owner, METH_VARARGS,
      "Receive multipart payload parts as a native bytes owner." },
    { "subscribe_parts", py_subscribe_parts, METH_VARARGS,
      "Receive topic multipart payload parts through zlink_subscribe_part." },
    { "subscribe_owner", py_subscribe_owner, METH_VARARGS,
      "Receive topic multipart payload parts as a native owner." },
    { "router_recv_parts", py_router_recv_parts, METH_VARARGS,
      "Receive routed multipart payload parts through zlink_router_recv_part." },
    { "router_recv_owner", py_router_recv_owner, METH_VARARGS,
      "Receive routed multipart payload parts as a native owner." },
    { "spot_subscribe_parts", py_spot_subscribe_parts, METH_VARARGS,
      "Receive spot topic multipart payload parts through zlink_spot_subscribe_part." },
    { "spot_recv_parts", py_spot_recv_parts, METH_VARARGS,
      "Receive spot routed multipart payload parts through zlink_spot_recv_part." },
    { "single_socket_one_way", py_single_socket_one_way, METH_VARARGS,
      "Run a native single one-way socket benchmark active phase." },
    { "multi_send_one_way", py_multi_send_one_way, METH_VARARGS,
      "Run a native multi one-way send active phase." },
    { "multi_echo_roundtrip", py_multi_echo_roundtrip, METH_VARARGS,
      "Run a native multi routed echo active phase." },
    { "multi_spot_sendsend_roundtrip", py_multi_spot_sendsend_roundtrip,
      METH_VARARGS,
      "Run a native multi SPOT send/send echo active phase." },
    { "multi_spot_reqrep_roundtrip", py_multi_spot_reqrep_roundtrip,
      METH_VARARGS,
      "Run a native multi SPOT request/reply active phase." },
    { "spot_routed_echo_install", py_spot_routed_echo_install, METH_VARARGS,
      "Install a native SPOT routed echo dispatch handler." },
    { "spot_routed_echo_stats", py_spot_routed_echo_stats, METH_VARARGS,
      "Read native SPOT routed echo dispatch stats." },
    { "router_echo_server_loop", py_router_echo_server_loop, METH_VARARGS,
      "Run a native routed echo server loop until a stop flag is set." },
    { "recv_count_active", py_recv_count_active, METH_VARARGS,
      "Count active one-part received messages in a native loop." },
    { "publish_active", py_publish_active, METH_VARARGS,
      "Publish topic payloads in a native active phase." },
    { "subscribe_count_active", py_subscribe_count_active, METH_VARARGS,
      "Count active topic messages across subscribers in a native loop." },
    { "spot_subscribe_count_active", py_spot_subscribe_count_active,
      METH_VARARGS,
      "Count active SPOT topic messages across subscribers in a native loop." },
    { "spot_publish_active", py_spot_publish_active, METH_VARARGS,
      "Publish a native single SPOT active phase and stop token." },
    { "spot_count_install", py_spot_count_install, METH_VARARGS,
      "Install a native single SPOT subscribe counter." },
    { "spot_count_start", py_spot_count_start, METH_VARARGS,
      "Start the native single SPOT subscribe counter active window." },
    { "spot_count_stats", py_spot_count_stats, METH_VARARGS,
      "Return native single SPOT subscribe counter metrics." },
    { "stream_echo_install", py_stream_echo_install, METH_VARARGS,
      "Install a native stream packet echo handler for perf hot paths." },
    { "stream_echo_drain", py_stream_echo_drain, METH_VARARGS,
      "Drain pending native stream echo replies." },
    { "stream_echo_stats", py_stream_echo_stats, METH_VARARGS,
      "Return native stream echo counters." },
    { "perf_stamp_payload", py_perf_stamp_payload, METH_VARARGS,
      "Stamp a perf payload header." },
    { "perf_active_latency_ns", py_perf_active_latency_ns, METH_VARARGS,
      "Classify a perf payload and return active latency ns." },
    { NULL, NULL, 0, NULL },
};

static struct PyModuleDef zlink_native_module = {
    PyModuleDef_HEAD_INIT,
    "_zlink_native",
    "Private native bridge for zlink Python hot paths.",
    -1,
    zlink_native_methods,
};

PyMODINIT_FUNC
PyInit__zlink_native (void)
{
    PyObject *module = NULL;
    if (PyType_Ready (&socket_send_op_type) < 0)
        return NULL;
    if (PyType_Ready (&routed_send_op_type) < 0)
        return NULL;
    if (PyType_Ready (&publisher_send_op_type) < 0)
        return NULL;
    if (PyType_Ready (&bytes_parts_owner_type) < 0)
        return NULL;
    if (PyType_Ready (&native_parts_owner_type) < 0)
        return NULL;
    module = PyModule_Create (&zlink_native_module);
    if (!module)
        return NULL;
    Py_INCREF (&socket_send_op_type);
    if (PyModule_AddObject (module, "SocketSendOp",
                            (PyObject *) &socket_send_op_type)
        != 0) {
        Py_DECREF (&socket_send_op_type);
        Py_DECREF (module);
        return NULL;
    }
    Py_INCREF (&routed_send_op_type);
    if (PyModule_AddObject (module, "RoutedSendOp",
                            (PyObject *) &routed_send_op_type)
        != 0) {
        Py_DECREF (&routed_send_op_type);
        Py_DECREF (module);
        return NULL;
    }
    Py_INCREF (&publisher_send_op_type);
    if (PyModule_AddObject (module, "PublisherSendOp",
                            (PyObject *) &publisher_send_op_type)
        != 0) {
        Py_DECREF (&publisher_send_op_type);
        Py_DECREF (module);
        return NULL;
    }
    Py_INCREF (&bytes_parts_owner_type);
    if (PyModule_AddObject (module, "BytesReceivedPartsOwner",
                            (PyObject *) &bytes_parts_owner_type)
        != 0) {
        Py_DECREF (&bytes_parts_owner_type);
        Py_DECREF (module);
        return NULL;
    }
    Py_INCREF (&native_parts_owner_type);
    if (PyModule_AddObject (module, "NativeReceivedPartsOwner",
                            (PyObject *) &native_parts_owner_type)
        != 0) {
        Py_DECREF (&native_parts_owner_type);
        Py_DECREF (module);
        return NULL;
    }
    return module;
}
