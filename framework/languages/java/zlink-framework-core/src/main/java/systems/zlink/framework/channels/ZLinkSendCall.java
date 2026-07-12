package systems.zlink.framework.channels;

public interface ZLinkSendCall {
    ZLinkSendCall metadata(String key, String value);

    void submit();
}
