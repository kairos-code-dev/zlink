package systems.zlink.stream.connector;

import systems.zlink.framework.runtime.streams.ZLinkStreamPayloadCompression;

final class ZLinkStreamLz4Pickler {
    private ZLinkStreamLz4Pickler() {
    }

    static byte[] pickle(byte[] source) {
        return ZLinkStreamPayloadCompression.lz4Pickle(source);
    }

    static byte[] unpickle(byte[] source) {
        return ZLinkStreamPayloadCompression.lz4Unpickle(source);
    }

    static byte[] unpickle(byte[] source, int maxDecompressedSize) {
        return ZLinkStreamPayloadCompression.lz4Unpickle(source, maxDecompressedSize);
    }
}
