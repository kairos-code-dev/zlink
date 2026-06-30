package systems.zlink.framework.streams;

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);

    ZLinkSessionReplyCall compress();

    void submit();
}
