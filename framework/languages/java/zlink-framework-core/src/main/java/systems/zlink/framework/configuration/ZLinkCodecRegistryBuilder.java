package systems.zlink.framework.configuration;

public interface ZLinkCodecRegistryBuilder {
    void addJson();

    void addMessagePack();

    void addProtobuf();
}
