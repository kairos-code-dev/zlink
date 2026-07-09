/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.DurationConversions;
import systems.zlink.runtime.nativeapi.NativeIntOptions;
import java.time.Duration;
import java.util.Objects;

final class SpotOptions {
    private static final int OPT_REQUEST_TIMEOUT_MS = 0x3701;

    private final Spot spot;

    public SpotOptions(Spot spot) {
        this.spot = spot;
    }

    public Duration requestTimeout() {
        return Duration.ofMillis(getIntOption(OPT_REQUEST_TIMEOUT_MS));
    }

    public void requestTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        setIntOption(OPT_REQUEST_TIMEOUT_MS, DurationConversions.toIntMillis(value, "value"));
    }

    private int getIntOption(int option) {
        return NativeIntOptions.get(InternalAccess.spotHandle(spot), option,
            Native::getSpotOption);
    }

    private void setIntOption(int option, int value) {
        NativeIntOptions.set(InternalAccess.spotHandle(spot), option, value,
            Native::setSpotOption);
    }
}
