/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import java.lang.foreign.MemorySegment;
import java.util.Objects;
import java.util.Optional;

public final class ActorJoinInfo {
    private final ActorRef sourceActor;
    private final ActorRef targetActor;
    private final RoutingId sourceNodeRid;
    private final RoutingId sourceSpotRid;
    private final RoutingId targetNodeRid;
    private final RoutingId targetSpotRid;
    private final long joinEpoch;
    private final MemorySegment request;
    private final int flags;

    public ActorJoinInfo(ActorRef actor, RoutingId sourceNodeRid, int flags) {
        this(actor, actor, sourceNodeRid, null, null, null, 0L,
          MemorySegment.NULL, flags);
    }

    static ActorJoinInfo fromNative(ActorRef sourceActor,
                                    ActorRef targetActor,
                                    RoutingId sourceNodeRid,
                                    RoutingId sourceSpotRid,
                                    RoutingId targetNodeRid,
                                    RoutingId targetSpotRid,
                                    long joinEpoch,
                                    MemorySegment request,
                                    int flags) {
        return new ActorJoinInfo(sourceActor, targetActor, sourceNodeRid,
          sourceSpotRid, targetNodeRid, targetSpotRid, joinEpoch, request,
          flags);
    }

    private ActorJoinInfo(ActorRef sourceActor, ActorRef targetActor,
                          RoutingId sourceNodeRid, RoutingId sourceSpotRid,
                          RoutingId targetNodeRid, RoutingId targetSpotRid,
                          long joinEpoch, MemorySegment request, int flags) {
        this.sourceActor = Objects.requireNonNull(sourceActor, "sourceActor");
        this.targetActor = Objects.requireNonNull(targetActor, "targetActor");
        this.sourceNodeRid = sourceNodeRid;
        this.sourceSpotRid = sourceSpotRid;
        this.targetNodeRid = targetNodeRid;
        this.targetSpotRid = targetSpotRid;
        this.joinEpoch = joinEpoch;
        this.request = request == null ? MemorySegment.NULL : request;
        this.flags = flags;
    }

    public ActorRef actor() {
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

    MemorySegment request() {
        return request;
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
