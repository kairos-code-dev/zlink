# SPDX-License-Identifier: MPL-2.0

_common_socket_options_factory = None
_dealer_socket_options_factory = None
_stream_socket_options_factory = None
_sub_socket_options_factory = None
_pub_socket_options_factory = None
_router_socket_options_factory = None


def register_socket_option_factories(
    *,
    common_socket_options_factory,
    dealer_socket_options_factory,
    stream_socket_options_factory,
    sub_socket_options_factory,
    pub_socket_options_factory,
    router_socket_options_factory,
):
    global _common_socket_options_factory
    global _dealer_socket_options_factory
    global _stream_socket_options_factory
    global _sub_socket_options_factory
    global _pub_socket_options_factory
    global _router_socket_options_factory
    _common_socket_options_factory = common_socket_options_factory
    _dealer_socket_options_factory = dealer_socket_options_factory
    _stream_socket_options_factory = stream_socket_options_factory
    _sub_socket_options_factory = sub_socket_options_factory
    _pub_socket_options_factory = pub_socket_options_factory
    _router_socket_options_factory = router_socket_options_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


def _stub_get(self):
    raise NotImplementedError


def _stub_set(self, value):
    raise NotImplementedError


def _contract_property():
    return property(_stub_get, _stub_set)


class CommonSocketOptions:
    def __new__(cls, socket):
        if cls is CommonSocketOptions:
            return _require(_common_socket_options_factory, "common socket options")(
                socket
            )
        return super().__new__(cls)

    linger_ms = _contract_property()
    send_high_water_mark = _contract_property()
    receive_high_water_mark = _contract_property()
    send_timeout_ms = _contract_property()
    receive_timeout_ms = _contract_property()
    immediate = _contract_property()
    rid_duplicate_policy = _contract_property()
    connect_timeout_ms = _contract_property()
    ipv6 = _contract_property()
    tcp_no_delay = _contract_property()
    tcp_keepalive = _contract_property()
    heartbeat_interval_ms = _contract_property()
    heartbeat_ttl_ms = _contract_property()
    heartbeat_timeout_ms = _contract_property()
    max_msg_size = _contract_property()
    backlog = _contract_property()
    reconnect_interval_ms = _contract_property()
    reconnect_interval_max_ms = _contract_property()


class DealerSocketOptions:
    def __new__(cls, socket):
        if cls is DealerSocketOptions:
            return _require(_dealer_socket_options_factory, "dealer socket options")(
                socket
            )
        return super().__new__(cls)

    probe = _contract_property()
    weight = _contract_property()
    request_timeout_ms = _contract_property()


class StreamSocketOptions:
    def __new__(cls, socket):
        if cls is StreamSocketOptions:
            return _require(_stream_socket_options_factory, "stream socket options")(
                socket
            )
        return super().__new__(cls)

    notify = _contract_property()


class SubSocketOptions:
    def __new__(cls, socket):
        if cls is SubSocketOptions:
            return _require(_sub_socket_options_factory, "sub socket options")(socket)
        return super().__new__(cls)

    @property
    def topics_count(self): ...


class PubSocketOptions:
    def __new__(cls, socket):
        if cls is PubSocketOptions:
            return _require(_pub_socket_options_factory, "pub socket options")(socket)
        return super().__new__(cls)

    verbose = _contract_property()
    verboser = _contract_property()
    manual = _contract_property()
    no_drop = _contract_property()
    manual_last_value = _contract_property()
    welcome_message = _contract_property()

    @property
    def topics_count(self): ...

    def approve_subscribe(self, routing_id): ...

    def reject_subscribe(self, routing_id): ...


class RouterSocketOptions:
    def __new__(cls, socket):
        if cls is RouterSocketOptions:
            return _require(_router_socket_options_factory, "router socket options")(
                socket
            )
        return super().__new__(cls)

    mandatory = _contract_property()
    handover = _contract_property()
    probe = _contract_property()
    connect_routing_id = _contract_property()
    weight = _contract_property()
    request_timeout_ms = _contract_property()
