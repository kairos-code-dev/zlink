module systems.zlink {
    requires static io.netty.buffer;
    requires jdk.unsupported;

    exports systems.zlink.contracts;
    exports systems.zlink.contracts.service.discovery;
    exports systems.zlink.contracts.service.registry;
    exports systems.zlink.contracts.service.spot;
}
