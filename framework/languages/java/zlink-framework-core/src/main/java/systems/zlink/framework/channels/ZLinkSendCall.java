package systems.zlink.framework.channels;

public interface ZLinkSendCall {
    ZLinkSendCall packetName(String packetName);

    ZLinkSendCall metadata(String key, String value);

    void submit();
}
