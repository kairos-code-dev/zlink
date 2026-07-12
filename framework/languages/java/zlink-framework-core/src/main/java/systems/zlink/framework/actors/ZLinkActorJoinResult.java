package systems.zlink.framework.actors;

public sealed interface ZLinkActorJoinResult<TReply>
    permits ZLinkActorJoinResult.Accepted, ZLinkActorJoinResult.Rejected {

    TReply reply();

    record Accepted<TReply>(ActorRef actor, TReply reply)
        implements ZLinkActorJoinResult<TReply> {
    }

    record Rejected<TReply>(TReply reply)
        implements ZLinkActorJoinResult<TReply> {
    }
}
