module systems.zlink {
    requires static io.netty.buffer;
    requires jdk.unsupported;

    exports systems.zlink.contracts.core;
    exports systems.zlink.contracts.errors;
    exports systems.zlink.contracts.eventing;
    exports systems.zlink.contracts.messaging;
    exports systems.zlink.contracts.sockets;
}
