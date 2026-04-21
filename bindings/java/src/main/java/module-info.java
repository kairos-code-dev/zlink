module dev.kairoscode.zlink {
    requires static io.netty.buffer;
    requires jdk.unsupported;

    exports dev.kairoscode.zlink;
    exports dev.kairoscode.zlink.service.discovery;
    exports dev.kairoscode.zlink.service.registry;
    exports dev.kairoscode.zlink.service.spot;
}
