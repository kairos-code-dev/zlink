/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;

final class SpotNodeOptionsSupport {
    private static final int OPT_SNDHWM = 23;
    private static final int OPT_RCVHWM = 24;
    private static final int OPT_ROUTER_HWM_PROFILE = 0x360E;
    private static final int OPT_ROUTER_HWM = 0x360F;
    private static final int OPT_PUBSUB_HWM_PROFILE = 0x3610;
    private static final int OPT_PUBSUB_HWM = 0x3611;
    private static final int OPT_DISPATCH_WORKERS_MIN = 0x3612;
    private static final int OPT_DISPATCH_WORKERS_MAX = 0x3613;

    private final NativeSpotNode node;

    SpotNodeOptionsSupport(NativeSpotNode node) {
        this.node = Objects.requireNonNull(node, "node");
    }

    void sendHwm(int value) {
        setPubSubIntOption(OPT_SNDHWM, value, true);
    }

    void recvHwm(int value) {
        setPubSubIntOption(OPT_RCVHWM, value, false);
    }

    AutoHwmProfile routerHwmProfile() {
        return EnumCodecs.autoHwmProfileFromValue(
            getNodeIntOption(OPT_ROUTER_HWM_PROFILE));
    }

    void routerHwmProfile(AutoHwmProfile profile) {
        Objects.requireNonNull(profile, "profile");
        setNodeIntOption(OPT_ROUTER_HWM_PROFILE,
            EnumCodecs.autoHwmProfileValue(profile));
    }

    int routerHighWaterMark() {
        return getNodeIntOption(OPT_ROUTER_HWM);
    }

    void routerHighWaterMark(int value) {
        setNodeIntOption(OPT_ROUTER_HWM, value);
    }

    AutoHwmProfile pubSubHwmProfile() {
        return EnumCodecs.autoHwmProfileFromValue(
            getNodeIntOption(OPT_PUBSUB_HWM_PROFILE));
    }

    void pubSubHwmProfile(AutoHwmProfile profile) {
        Objects.requireNonNull(profile, "profile");
        setNodeIntOption(OPT_PUBSUB_HWM_PROFILE,
            EnumCodecs.autoHwmProfileValue(profile));
    }

    int pubSubHighWaterMark() {
        return getNodeIntOption(OPT_PUBSUB_HWM);
    }

    void pubSubHighWaterMark(int value) {
        setNodeIntOption(OPT_PUBSUB_HWM, value);
    }

    int dispatchWorkersMin() {
        return getNodeIntOption(OPT_DISPATCH_WORKERS_MIN);
    }

    void dispatchWorkersMin(int value) {
        setNodeIntOption(OPT_DISPATCH_WORKERS_MIN, value);
    }

    int dispatchWorkersMax() {
        return getNodeIntOption(OPT_DISPATCH_WORKERS_MAX);
    }

    void dispatchWorkersMax(int value) {
        setNodeIntOption(OPT_DISPATCH_WORKERS_MAX, value);
    }

    private int getNodeIntOption(int option) {
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSpotNodeOption(node.handle(), option,
                nativeValue, len);
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            return nativeValue.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private void setNodeIntOption(int option, int value) {
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setSpotNodeOption(node.handle(), option,
                nativeValue, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
        }
    }

    private void setPubSubIntOption(int optionId, int value,
                                    boolean publishOption) {
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = publishOption
                ? Native.setPubOption(node.handle(), optionId, nativeValue,
                    Integer.BYTES)
                : Native.setSubOption(node.handle(), optionId, nativeValue,
                    Integer.BYTES);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(publishOption
                    ? "zlink_set_pub_option"
                    : "zlink_set_sub_option");
            }
        }
    }
}
