package systems.zlink.samples.gamequest.client.contracts;

import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

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
