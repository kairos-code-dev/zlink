package systems.zlink.framework.streams;

public interface ZLinkSessionSendCall {
    ZLinkSessionSendCall metadata(String key, String value);

    ZLinkSessionSendCall packetName(String messageName);

    ZLinkSessionSendCall compress();

    void submit();
}
