module systems.zlink {
    requires static io.netty.buffer;
    requires jdk.unsupported;

    exports systems.zlink;
    exports systems.zlink.service.discovery;
    exports systems.zlink.service.registry;
    exports systems.zlink.service.spot;
}
