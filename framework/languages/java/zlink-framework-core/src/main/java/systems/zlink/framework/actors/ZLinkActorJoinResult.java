package systems.zlink.framework.actors;

public record ZLinkActorJoinResult<TReply>(
    int resultCode,
    ZLinkActorRef actor,
    TReply reply) {
}
