package systems.zlink.framework.actors;

public record ZLinkActorJoinResult<TReply>(
    int resultCode,
    ActorRef actor,
    TReply reply) {
    public boolean accepted() {
        return resultCode == 0;
    }

    public java.util.Optional<ActorRef> actorRef() {
        return java.util.Optional.ofNullable(actor);
    }
}
