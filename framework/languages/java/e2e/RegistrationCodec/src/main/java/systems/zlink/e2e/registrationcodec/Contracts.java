package systems.zlink.e2e.registrationcodec;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class Contracts {
    public static final String CHANNEL = "registration.codec.api";
    public static final String AUTO_GROUP = "registration-codec-auto";
    public static final String ATTR_GROUP = "registration-codec-attr";

    private Contracts() {
    }

    @ZLinkPacket("EchoAuto")
    public record EchoAutoRequest(String value) {
    }

    @ZLinkPacket("EchoAuto")
    public record EchoAutoCommand(String value) {
    }

    public record EchoAttrRequest(String value) {
    }

    public record EchoAttrCommand(String value) {
    }

    public record EchoManualRequest(String value) {
    }

    public record EchoManualCommand(String value) {
    }

    public record JsonEchoRequest(String value) {
    }

    public record JsonEchoCommand(String value) {
    }

    public record PackedEchoRequest(String value) {
    }

    public record PackedEchoReply(String value) {
    }

    public record PackedEchoCommand(String value) {
    }

    public record EchoReply(String value, String handler) {
    }

    public record EvidenceEntry(String marker, String packetName, String value) {
    }

    public record EvidenceSnapshot(List<EvidenceEntry> entries) {
    }
}
