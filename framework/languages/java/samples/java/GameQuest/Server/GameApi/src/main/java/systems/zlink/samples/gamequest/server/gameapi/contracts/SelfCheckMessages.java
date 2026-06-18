package systems.zlink.samples.gamequest.server.gameapi.contracts;

import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Self-check wire contracts hosted on the GameApi action channel.
 *
 * <p>The .NET sample exposes these as HTTP self-check endpoints with path
 * parameters; the Java sample mirrors them as ZLink request/reply packets so the
 * client scenario can drive the same recovery and reconciliation checks. The
 * client mirrors structurally-identical records under its own configuration
 * package; the JSON codec matches them by packet name and field layout.
 */
public final class SelfCheckMessages {
    private SelfCheckMessages() {
    }

    @ZLinkPacket("KillWithoutPublishReq")
    public record KillWithoutPublishReq(String playerId) {
    }

    public record KillWithoutPublishRes(boolean accepted) {
    }

    @ZLinkPacket("DeleteProjectionReq")
    public record DeleteProjectionReq(String playerId, String questId) {
    }

    public record DeleteProjectionRes(boolean deleted) {
    }

    @ZLinkPacket("RebuildProjectionReq")
    public record RebuildProjectionReq(String playerId, String questId) {
    }

    public record RebuildProjectionRes(Messages.QuestProgress state) {
    }

    @ZLinkPacket("GameQuestServerAssertReq")
    public record GameQuestServerAssertReq() {
    }
}
