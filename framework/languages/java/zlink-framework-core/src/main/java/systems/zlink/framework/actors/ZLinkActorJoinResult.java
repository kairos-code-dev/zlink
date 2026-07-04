package systems.zlink.framework.actors;

public record ZLinkActorJoinResult<TReply>(
    int resultCode,
    ZLinkActorRef actor,
    TReply reply) {
    public boolean accepted() {
        return resultCode == 1;
    }

    public java.util.Optional<ZLinkActorRef> actorRef() {
        return java.util.Optional.ofNullable(actor);
    }
}
