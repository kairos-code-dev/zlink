/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.internal.ContractAccess;
import systems.zlink.contracts.messaging.Message;
import java.util.Objects;
import java.util.Optional;

/** Details of an actor-join request: the actors and spots on each side. */
public final class ActorJoinInfo {
    private final ActorRef sourceActor;
    private final ActorRef targetActor;
    private final RoutingId sourceNodeRid;
    private final RoutingId sourceSpotRid;
    private final RoutingId targetNodeRid;
    private final RoutingId targetSpotRid;
    private final long joinEpoch;
    private final Object requestState;
    private final int flags;

    static {
        ContractAccess.register(new ContractAccess.ActorJoinAccess() {
            @Override
            public ActorJoinRequest request(ActorJoinInfo info,
                                            Message message) {
                return new ActorJoinRequest(info, message);
            }

            @Override
            public ActorJoinRequest request(ActorJoinInfo info,
                                            java.util.List<Message> parts) {
                return new ActorJoinRequest(info, parts);
            }

            @Override
            public ActorJoinInfo infoFromNative(
              ActorRef sourceActor,
              ActorRef targetActor,
              RoutingId sourceNodeRid,
              RoutingId sourceSpotRid,
              RoutingId targetNodeRid,
              RoutingId targetSpotRid,
              long joinEpoch,
              Object requestState,
              int flags) {
                return ActorJoinInfo.fromNative(sourceActor, targetActor,
                    sourceNodeRid, sourceSpotRid, targetNodeRid, targetSpotRid,
                    joinEpoch, requestState, flags);
            }

            @Override
            public Object requestState(ActorJoinInfo info) {
                return info.requestState();
            }

            @Override
            public RoutingId sourceNodeRidRaw(ActorJoinInfo info) {
                return info.sourceNodeRidRaw();
            }

            @Override
            public RoutingId sourceSpotRidRaw(ActorJoinInfo info) {
                return info.sourceSpotRidRaw();
            }

            @Override
            public RoutingId targetNodeRidRaw(ActorJoinInfo info) {
                return info.targetNodeRidRaw();
            }

            @Override
            public RoutingId targetSpotRidRaw(ActorJoinInfo info) {
                return info.targetSpotRidRaw();
            }

            @Override
            public SpotActorLifecycleEventKind lifecycleKindFromValue(
              int value) {
                return SpotActorLifecycleEventKind.fromValue(value);
            }
        });
    }

    ActorJoinInfo(ActorRef actor, RoutingId sourceNodeRid, int flags) {
        this(actor, actor, sourceNodeRid, null, null, null, 0L,
          null, flags);
    }

    static ActorJoinInfo fromNative(ActorRef sourceActor,
                                    ActorRef targetActor,
                                    RoutingId sourceNodeRid,
                                    RoutingId sourceSpotRid,
                                    RoutingId targetNodeRid,
                                    RoutingId targetSpotRid,
                                    long joinEpoch,
                                    Object requestState,
                                    int flags) {
        return new ActorJoinInfo(sourceActor, targetActor, sourceNodeRid,
          sourceSpotRid, targetNodeRid, targetSpotRid, joinEpoch, requestState,
          flags);
    }

    private ActorJoinInfo(ActorRef sourceActor, ActorRef targetActor,
                          RoutingId sourceNodeRid, RoutingId sourceSpotRid,
                          RoutingId targetNodeRid, RoutingId targetSpotRid,
                          long joinEpoch, Object requestState, int flags) {
        this.sourceActor = Objects.requireNonNull(sourceActor, "sourceActor");
        this.targetActor = Objects.requireNonNull(targetActor, "targetActor");
        this.sourceNodeRid = sourceNodeRid;
        this.sourceSpotRid = sourceSpotRid;
        this.targetNodeRid = targetNodeRid;
        this.targetSpotRid = targetSpotRid;
        this.joinEpoch = joinEpoch;
        this.requestState = requestState;
        this.flags = flags;
    }

    ActorRef actor() {
        return sourceActor;
    }

    public ActorRef sourceActor() {
        return sourceActor;
    }

    public ActorRef targetActor() {
        return targetActor;
    }

    public Optional<RoutingId> sourceNodeRid() {
        return Optional.ofNullable(sourceNodeRid);
    }

    public Optional<RoutingId> sourceSpotRid() {
        return Optional.ofNullable(sourceSpotRid);
    }

    public Optional<RoutingId> targetNodeRid() {
        return Optional.ofNullable(targetNodeRid);
    }

    public Optional<RoutingId> targetSpotRid() {
        return Optional.ofNullable(targetSpotRid);
    }

    public long joinEpoch() {
        return joinEpoch;
    }

    public int flags() {
        return flags;
    }

    Object requestState() {
        return requestState;
    }

    RoutingId sourceNodeRidRaw() {
        return sourceNodeRid;
    }

    RoutingId sourceSpotRidRaw() {
        return sourceSpotRid;
    }

    RoutingId targetNodeRidRaw() {
        return targetNodeRid;
    }

    RoutingId targetSpotRidRaw() {
        return targetSpotRid;
    }
}
