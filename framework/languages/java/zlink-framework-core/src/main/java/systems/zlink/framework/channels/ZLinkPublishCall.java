package systems.zlink.framework.channels;

public interface ZLinkPublishCall {
    ZLinkPublishCall packetName(String packetName);

    ZLinkPublishCall metadata(String key, String value);

    void submit();
}
