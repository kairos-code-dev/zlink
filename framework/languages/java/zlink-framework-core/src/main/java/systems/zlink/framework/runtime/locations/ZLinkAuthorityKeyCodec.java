package systems.zlink.framework.runtime.locations;

public final class ZLinkAuthorityKeyCodec {
    private static final char[] HEX = "0123456789ABCDEF".toCharArray();
    private ZLinkAuthorityKeyCodec() {
    }

    public static String spot(systems.zlink.contracts.core.RoutingId spotRid) {
        byte[] identity =
            java.util.Objects.requireNonNull(spotRid, "spotRid")
                .toBytes();
        if (identity.length == 0 || identity.length > 0xff) {
            throw new IllegalArgumentException(
                "Spot authority identity must contain 1..255 bytes");
        }
        StringBuilder encoded = new StringBuilder(
            "zla1:s:" + identity.length + ":");
        for (byte item : identity) {
            int value = Byte.toUnsignedInt(item);
            if (isUnreserved(value)) {
                encoded.append((char) value);
            } else {
                encoded.append('%');
                encoded.append(HEX[(value >>> 4) & 0xf]);
                encoded.append(HEX[value & 0xf]);
            }
        }
        return encoded.toString();
    }

    static String spotPrefix() {
        return "zla1:s:";
    }

    private static boolean isUnreserved(int value) {
        return value >= 'A' && value <= 'Z'
            || value >= 'a' && value <= 'z'
            || value >= '0' && value <= '9'
            || value == '-'
            || value == '.'
            || value == '_'
            || value == '~';
    }
}
