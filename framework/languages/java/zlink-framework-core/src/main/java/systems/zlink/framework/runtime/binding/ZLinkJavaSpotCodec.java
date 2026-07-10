package systems.zlink.framework.runtime.binding;

import java.util.Optional;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.SpotActorLifecycleEvent;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotDispatchInfo;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleInfo;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchInfo;

final class ZLinkJavaSpotCodec {
    private ZLinkJavaSpotCodec() {
    }

    static ZLinkBackendSpotDispatchInfo fromSpotDispatchInfo(SpotDispatchInfo info) {
        return new ZLinkBackendSpotDispatchInfo(
            map(info.event()),
            info.actorMessages().stream()
                .map(ZLinkJavaSpotCodec::fromActorReceived)
                .toList());
    }

    static ZLinkBackendActorReceived fromActorReceived(ActorReceived received) {
        var info = received.info();
        return new ZLinkBackendActorReceived(
            fromActorRef(info.actor()),
            info.sourceNodeRid(),
            info.sourceSessionRid(),
            Optional.empty(),
            info.requestId(),
            info.flags(),
            Message.from(received.message()),
            received.hasMore());
    }

    static ZLinkBackendActorJoinRequest fromActorJoinRequest(ActorJoinRequest request) {
        return new ZLinkBackendActorJoinRequest(
            fromActorRef(request.info().sourceActor()),
            fromActorRef(request.info().targetActor()),
            request.parts().stream().map(Message::from).toList(),
            request);
    }

    static ZLinkBackendActorLifecycleEvent fromActorLifecycleEvent(
        SpotActorLifecycleEvent event) {
        var info = event.info();
        return new ZLinkBackendActorLifecycleEvent(
            ZLinkBackendActorLifecycleEventKind.valueOf(event.kind().name()),
            new ZLinkBackendActorLifecycleInfo(
                fromActorRef(info.previousActor()),
                fromActorRef(info.currentActor()),
                info.previousSpotRid(),
                info.currentSpotRid(),
                info.joinEpoch(),
                info.flags()));
    }

    static ZLinkBackendActorRef fromActorRef(ActorRef actorRef) {
        return new ZLinkBackendActorRef(actorRef.nodeRid(), actorRef.actorId(), actorRef.generation());
    }

    private static ZLinkBackendSpotDispatchEvent map(SpotDispatchEvent event) {
        return switch (event) {
            case SUBSCRIBE_READABLE -> ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE;
            case ROUTED_READABLE -> ZLinkBackendSpotDispatchEvent.ROUTED_READABLE;
            case TIMER_READABLE -> ZLinkBackendSpotDispatchEvent.TIMER_READABLE;
            case CHANNEL_REPLY_READABLE -> ZLinkBackendSpotDispatchEvent.CHANNEL_REPLY_READABLE;
            case ACTOR_READABLE -> ZLinkBackendSpotDispatchEvent.ACTOR_READABLE;
            case ACTOR_JOIN_READABLE -> ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE;
            case ACTOR_LIFECYCLE_READABLE -> ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE;
        };
    }
}
