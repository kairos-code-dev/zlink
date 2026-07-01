package systems.zlink.e2e.registrationcodec.shared;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class Contracts {
    public static final String CHANNEL = "registration.codec.api";
    public static final String AUTO_GROUP = "registration-codec-auto";
    public static final String ATTR_GROUP = "registration-codec-attr";

    private Contracts() {
    }

    @ZLinkPacket("EchoAuto")
    public record EchoAutoReq(String value) {
    }

    @ZLinkPacket("EchoAuto")
    public record EchoAutoMsg(String value) {
    }

    public record EchoAttrReq(String value) {
    }

    public record EchoAttrMsg(String value) {
    }

    public record EchoManualReq(String value) {
    }

    public record EchoManualMsg(String value) {
    }

    public record DiLifecycleReq(String value) {
    }

    public record DiLifecycleRes(
        String value,
        int scopedId,
        int singletonId,
        int disposedCount) {
    }

    public record JsonEchoReq(String value) {
    }

    public record JsonEchoMsg(String value) {
    }

    public record PackedEchoReq(String value) {
    }

    public record PackedEchoRes(String value) {
    }

    public record PackedEchoMsg(String value) {
    }

    public record EchoRes(String value, String handler) {
    }

    public record CodecScenarioRes(
        EchoRes json,
        String protobufValue,
        String messagePackValue) {
    }

    public record CodecMismatchProbeRes(
        boolean rejected,
        String errorType,
        String replyValue) {
    }

    public record EvidenceEntry(String marker, String packetName, String value) {
    }

    public record EvidenceSnapshot(List<EvidenceEntry> entries) {
    }
}
