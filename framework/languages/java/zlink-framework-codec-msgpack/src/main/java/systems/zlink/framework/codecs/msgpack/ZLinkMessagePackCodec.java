package systems.zlink.framework.codecs.msgpack;

import systems.zlink.framework.configuration.ZLinkCodecExtension;
import systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamTypedCodec;

public final class ZLinkMessagePackCodec implements ZLinkCodecExtension, ZLinkStreamTypedCodec {
    private static final ZLinkMessagePackCodec DEFAULT = new ZLinkMessagePackCodec();

    private ZLinkMessagePackCodec() {
    }

    public static ZLinkMessagePackCodec defaultCodec() {
        return DEFAULT;
    }

    @Override
    public <T> ZLinkStreamEncodedPayload encode(String packetName, T value) {
        return ZLinkMessagePackStreamCodec.INSTANCE.encode(packetName, value);
    }

    @Override
    public <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type) {
        return ZLinkMessagePackStreamCodec.INSTANCE.decode(payload, type);
    }

    @Override
    public void register(ZLinkCodecRegistryBuilder codecs) {
        codecs.addSerializer("application/x-msgpack", ZLinkMessagePackMessageSerializer.INSTANCE);
        codecs.addStreamCodec("application/x-msgpack", ZLinkStreamCodec.MESSAGE_PACK);
    }
}
