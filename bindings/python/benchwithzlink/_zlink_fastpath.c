#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <errno.h>
#include <stdint.h>

#include <zlink.h>

static int parse_socket_handle(PyObject *obj, void **out_sock)
{
    unsigned long long raw = PyLong_AsUnsignedLongLong(obj);
    if (PyErr_Occurred())
        return 0;
    *out_sock = (void *)(uintptr_t)raw;
    return 1;
}

static void set_zlink_error(const char *prefix)
{
    const int err = zlink_errno();
    const char *msg = zlink_strerror(err);
    PyErr_Format(
      PyExc_RuntimeError, "%s: %s", prefix, msg ? msg : "zlink error");
}

static PyObject *py_send_many_const(PyObject *self, PyObject *args)
{
    PyObject *sock_obj = NULL;
    Py_buffer payload = {0};
    int flags = 0;
    int count = 0;
    void *sock = NULL;

    if (!PyArg_ParseTuple(args, "Oy*ii", &sock_obj, &payload, &flags, &count))
        return NULL;

    if (!parse_socket_handle(sock_obj, &sock)) {
        PyBuffer_Release(&payload);
        return NULL;
    }
    if (count <= 0) {
        PyBuffer_Release(&payload);
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        if (zlink_send_const(sock, payload.buf, (size_t)payload.len, flags) < 0) {
            PyBuffer_Release(&payload);
            set_zlink_error("send_many_const failed");
            return NULL;
        }
    }

    PyBuffer_Release(&payload);
    return PyLong_FromLong(count);
}

static PyObject *py_gateway_send_many_const(PyObject *self, PyObject *args)
{
    PyObject *gateway_obj = NULL;
    const char *service = NULL;
    Py_buffer payload = {0};
    int flags = 0;
    int count = 0;
    void *gateway = NULL;

    if (!PyArg_ParseTuple(
          args, "Osy*ii", &gateway_obj, &service, &payload, &flags, &count))
        return NULL;

    if (!parse_socket_handle(gateway_obj, &gateway)) {
        PyBuffer_Release(&payload);
        return NULL;
    }
    if (count <= 0) {
        PyBuffer_Release(&payload);
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        zlink_msg_t part;
        if (zlink_msg_init_data(&part, payload.buf, (size_t)payload.len, NULL, NULL) != 0) {
            PyBuffer_Release(&payload);
            set_zlink_error("gateway_send_many_const init_data failed");
            return NULL;
        }
        if (zlink_gateway_send(gateway, service, &part, 1, flags) != 0) {
            zlink_msg_close(&part);
            PyBuffer_Release(&payload);
            set_zlink_error("gateway_send_many_const failed");
            return NULL;
        }
    }

    PyBuffer_Release(&payload);
    return PyLong_FromLong(count);
}

static PyObject *py_spot_publish_many_const(PyObject *self, PyObject *args)
{
    PyObject *pub_obj = NULL;
    const char *topic = NULL;
    Py_buffer payload = {0};
    int flags = 0;
    int count = 0;
    void *pub = NULL;

    if (!PyArg_ParseTuple(
          args, "Osy*ii", &pub_obj, &topic, &payload, &flags, &count))
        return NULL;

    if (!parse_socket_handle(pub_obj, &pub)) {
        PyBuffer_Release(&payload);
        return NULL;
    }
    if (count <= 0) {
        PyBuffer_Release(&payload);
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        zlink_msg_t part;
        if (zlink_msg_init_data(&part, payload.buf, (size_t)payload.len, NULL, NULL) != 0) {
            PyBuffer_Release(&payload);
            set_zlink_error("spot_publish_many_const init_data failed");
            return NULL;
        }
        if (zlink_spot_pub_publish(pub, topic, &part, 1, flags) != 0) {
            zlink_msg_close(&part);
            PyBuffer_Release(&payload);
            set_zlink_error("spot_publish_many_const failed");
            return NULL;
        }
    }

    PyBuffer_Release(&payload);
    return PyLong_FromLong(count);
}

static PyObject *py_spot_recv_many(PyObject *self, PyObject *args)
{
    PyObject *sub_obj = NULL;
    int flags = 0;
    int count = 0;
    void *sub = NULL;

    if (!PyArg_ParseTuple(args, "Oii", &sub_obj, &flags, &count))
        return NULL;

    if (!parse_socket_handle(sub_obj, &sub))
        return NULL;
    if (count <= 0) {
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char topic[256] = {0};
        size_t topic_len = sizeof(topic);
        if (zlink_spot_sub_recv(sub, &parts, &part_count, flags, topic, &topic_len) != 0) {
            set_zlink_error("spot_recv_many failed");
            return NULL;
        }
        zlink_msgv_close(parts, part_count);
    }

    return PyLong_FromLong(count);
}

static PyObject *py_recv_many_into(PyObject *self, PyObject *args)
{
    PyObject *sock_obj = NULL;
    PyObject *buffer_obj = NULL;
    Py_buffer buffer = {0};
    int flags = 0;
    int count = 0;
    void *sock = NULL;

    if (!PyArg_ParseTuple(args, "OOii", &sock_obj, &buffer_obj, &flags, &count))
        return NULL;

    if (!parse_socket_handle(sock_obj, &sock))
        return NULL;
    if (count <= 0) {
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }
    if (PyObject_GetBuffer(buffer_obj, &buffer, PyBUF_WRITABLE | PyBUF_C_CONTIGUOUS) != 0)
        return NULL;
    if (buffer.len <= 0) {
        PyBuffer_Release(&buffer);
        PyErr_SetString(PyExc_ValueError, "buffer must not be empty");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        if (zlink_recv(sock, buffer.buf, (size_t)buffer.len, flags) < 0) {
            PyBuffer_Release(&buffer);
            set_zlink_error("recv_many_into failed");
            return NULL;
        }
    }

    PyBuffer_Release(&buffer);
    return PyLong_FromLong(count);
}

static PyObject *py_send_routed_many_const(PyObject *self, PyObject *args)
{
    PyObject *sock_obj = NULL;
    Py_buffer routing_id = {0};
    Py_buffer payload = {0};
    int payload_flags = 0;
    int count = 0;
    void *sock = NULL;

    if (!PyArg_ParseTuple(
          args, "Oy*y*ii", &sock_obj, &routing_id, &payload, &payload_flags, &count))
        return NULL;

    if (!parse_socket_handle(sock_obj, &sock)) {
        PyBuffer_Release(&routing_id);
        PyBuffer_Release(&payload);
        return NULL;
    }
    if (count <= 0) {
        PyBuffer_Release(&routing_id);
        PyBuffer_Release(&payload);
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }
    const int routing_flags = ZLINK_SNDMORE | (payload_flags & ZLINK_DONTWAIT);

    for (int i = 0; i < count; ++i) {
        if (zlink_send(sock, routing_id.buf, (size_t)routing_id.len, routing_flags) < 0) {
            PyBuffer_Release(&routing_id);
            PyBuffer_Release(&payload);
            set_zlink_error("send_routed_many_const routing_id failed");
            return NULL;
        }
        if (zlink_send_const(sock, payload.buf, (size_t)payload.len, payload_flags) < 0) {
            PyBuffer_Release(&routing_id);
            PyBuffer_Release(&payload);
            set_zlink_error("send_routed_many_const payload failed");
            return NULL;
        }
    }

    PyBuffer_Release(&routing_id);
    PyBuffer_Release(&payload);
    return PyLong_FromLong(count);
}

static PyObject *py_recv_pair_many_into(PyObject *self, PyObject *args)
{
    PyObject *sock_obj = NULL;
    PyObject *first_obj = NULL;
    PyObject *second_obj = NULL;
    Py_buffer first = {0};
    Py_buffer second = {0};
    int flags = 0;
    int count = 0;
    void *sock = NULL;

    if (!PyArg_ParseTuple(
          args, "OOOii", &sock_obj, &first_obj, &second_obj, &flags, &count))
        return NULL;

    if (!parse_socket_handle(sock_obj, &sock))
        return NULL;
    if (count <= 0) {
        PyErr_SetString(PyExc_ValueError, "count must be > 0");
        return NULL;
    }
    if (PyObject_GetBuffer(first_obj, &first, PyBUF_WRITABLE | PyBUF_C_CONTIGUOUS) != 0)
        return NULL;
    if (PyObject_GetBuffer(second_obj, &second, PyBUF_WRITABLE | PyBUF_C_CONTIGUOUS) != 0) {
        PyBuffer_Release(&first);
        return NULL;
    }
    if (first.len <= 0 || second.len <= 0) {
        PyBuffer_Release(&first);
        PyBuffer_Release(&second);
        PyErr_SetString(PyExc_ValueError, "buffers must not be empty");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        if (zlink_recv(sock, first.buf, (size_t)first.len, flags) < 0) {
            PyBuffer_Release(&first);
            PyBuffer_Release(&second);
            set_zlink_error("recv_pair_many_into first frame failed");
            return NULL;
        }
        if (zlink_recv(sock, second.buf, (size_t)second.len, flags) < 0) {
            PyBuffer_Release(&first);
            PyBuffer_Release(&second);
            set_zlink_error("recv_pair_many_into second frame failed");
            return NULL;
        }
    }

    PyBuffer_Release(&first);
    PyBuffer_Release(&second);
    return PyLong_FromLong(count);
}

static PyObject *py_recv_pair_drain_into(PyObject *self, PyObject *args)
{
    PyObject *sock_obj = NULL;
    PyObject *first_obj = NULL;
    PyObject *second_obj = NULL;
    Py_buffer first = {0};
    Py_buffer second = {0};
    int max_count = 0;
    void *sock = NULL;

    if (!PyArg_ParseTuple(args, "OOOi", &sock_obj, &first_obj, &second_obj, &max_count))
        return NULL;

    if (!parse_socket_handle(sock_obj, &sock))
        return NULL;
    if (max_count <= 0) {
        PyErr_SetString(PyExc_ValueError, "max_count must be > 0");
        return NULL;
    }
    if (PyObject_GetBuffer(first_obj, &first, PyBUF_WRITABLE | PyBUF_C_CONTIGUOUS) != 0)
        return NULL;
    if (PyObject_GetBuffer(second_obj, &second, PyBUF_WRITABLE | PyBUF_C_CONTIGUOUS) != 0) {
        PyBuffer_Release(&first);
        return NULL;
    }
    if (first.len <= 0 || second.len <= 0) {
        PyBuffer_Release(&first);
        PyBuffer_Release(&second);
        PyErr_SetString(PyExc_ValueError, "buffers must not be empty");
        return NULL;
    }

    int drained = 0;
    for (int i = 0; i < max_count; ++i) {
        if (zlink_recv(sock, first.buf, (size_t)first.len, ZLINK_DONTWAIT) < 0) {
            const int err = zlink_errno();
            if (err == EAGAIN
#ifdef EWOULDBLOCK
                || err == EWOULDBLOCK
#endif
            ) {
                break;
            }
            PyBuffer_Release(&first);
            PyBuffer_Release(&second);
            set_zlink_error("recv_pair_drain_into first frame failed");
            return NULL;
        }
        if (zlink_recv(sock, second.buf, (size_t)second.len, ZLINK_DONTWAIT) < 0) {
            const int err = zlink_errno();
            PyBuffer_Release(&first);
            PyBuffer_Release(&second);
            if (err == EAGAIN
#ifdef EWOULDBLOCK
                || err == EWOULDBLOCK
#endif
            ) {
                PyErr_SetString(PyExc_RuntimeError, "recv_pair_drain_into incomplete multipart frame");
                return NULL;
            }
            set_zlink_error("recv_pair_drain_into second frame failed");
            return NULL;
        }
        drained++;
    }

    PyBuffer_Release(&first);
    PyBuffer_Release(&second);
    return PyLong_FromLong(drained);
}

static PyMethodDef fastpath_methods[] = {
    {"send_many_const", py_send_many_const, METH_VARARGS, "Send the same payload many times."},
    {"gateway_send_many_const",
     py_gateway_send_many_const,
     METH_VARARGS,
     "Send gateway [payload] many times with a fixed service name."},
    {"spot_publish_many_const",
     py_spot_publish_many_const,
     METH_VARARGS,
     "Publish spot [payload] many times with a fixed topic."},
    {"spot_recv_many",
     py_spot_recv_many,
     METH_VARARGS,
     "Receive spot messages many times and release native parts."},
    {"recv_many_into", py_recv_many_into, METH_VARARGS, "Receive into the same buffer many times."},
    {"send_routed_many_const",
     py_send_routed_many_const,
     METH_VARARGS,
     "Send [routing_id, payload] multipart many times."},
    {"recv_pair_many_into",
     py_recv_pair_many_into,
     METH_VARARGS,
     "Receive two-frame multipart many times into fixed buffers."},
    {"recv_pair_drain_into",
     py_recv_pair_drain_into,
     METH_VARARGS,
     "Drain available two-frame multipart messages using DONTWAIT."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef fastpath_module = {
    PyModuleDef_HEAD_INIT,
    "_zlink_fastpath",
    "zlink benchmark fast-path extension",
    -1,
    fastpath_methods,
};

PyMODINIT_FUNC PyInit__zlink_fastpath(void)
{
    return PyModule_Create(&fastpath_module);
}
