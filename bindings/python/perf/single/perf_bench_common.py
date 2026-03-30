import os


def _load_fastpath_extension():
    if os.environ.get("BENCH_PY_FASTPATH_CEXT", "1") == "0":
        return None
    return None


FASTPATH_CEXT = _load_fastpath_extension()


def _factory_or_none(builder):
    if FASTPATH_CEXT is None:
        return None
    return builder(FASTPATH_CEXT)


def make_cext_send_many_const(socket, payload):
    return _factory_or_none(lambda cext: cext.make_send_many_const(socket, payload))


def make_cext_recv_many_into(socket, recv_buf):
    return _factory_or_none(lambda cext: cext.make_recv_many_into(socket, recv_buf))


def make_cext_send_routed_many_const(socket, routing_id, payload):
    return _factory_or_none(
        lambda cext: cext.make_send_routed_many_const(socket, routing_id, payload)
    )


def make_cext_recv_pair_many_into(socket, first_buf, second_buf):
    return _factory_or_none(
        lambda cext: cext.make_recv_pair_many_into(socket, first_buf, second_buf)
    )


def make_cext_recv_pair_drain_into(socket, first_buf, second_buf):
    return _factory_or_none(
        lambda cext: cext.make_recv_pair_drain_into(socket, first_buf, second_buf)
    )


def make_cext_spot_publish_many_const(spot, topic, payload):
    return _factory_or_none(
        lambda cext: cext.make_spot_publish_many_const(spot, topic, payload)
    )


def make_cext_spot_recv_many(spot):
    return _factory_or_none(lambda cext: cext.make_spot_recv_many(spot))
