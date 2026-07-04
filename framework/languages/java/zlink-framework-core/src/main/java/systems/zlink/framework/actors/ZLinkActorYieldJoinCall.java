package systems.zlink.framework.actors;

import systems.zlink.framework.CancellationToken;

public interface ZLinkActorYieldJoinCall extends ZLinkActorJoinCall {
    ZLinkActorJoinResult<Void> yield();

    ZLinkActorJoinResult<Void> yield(CancellationToken cancellationToken);

    <TReply> ZLinkActorJoinResult<TReply> yield(Class<TReply> replyType);

    <TReply> ZLinkActorJoinResult<TReply> yield(
        Class<TReply> replyType,
        CancellationToken cancellationToken);
}
