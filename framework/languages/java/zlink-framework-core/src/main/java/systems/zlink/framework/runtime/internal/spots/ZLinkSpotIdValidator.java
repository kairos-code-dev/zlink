package systems.zlink.framework.runtime.internal.spots;

import java.nio.charset.StandardCharsets;

/**
 * Validates the Framework SpotId text contract without changing the supplied
 * identity. In particular, validation does not normalize Unicode or fold case.
 */
public final class ZLinkSpotIdValidator {
    private ZLinkSpotIdValidator() {
    }

    public static String requireValid(String spotId) {
        if (spotId == null || spotId.indexOf('\0') >= 0) {
            throw new IllegalArgumentException(
                "SpotId must contain 1..255 UTF-8 bytes");
        }
        int byteLength = spotId.getBytes(StandardCharsets.UTF_8).length;
        if (byteLength == 0 || byteLength > 0xff) {
            throw new IllegalArgumentException(
                "SpotId must contain 1..255 UTF-8 bytes");
        }
        return spotId;
    }
}
