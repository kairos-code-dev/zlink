/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;

public final class NativeActor implements Actor {
    private final NativeMeshNode owner;
    private final ActorRef ref;

    NativeActor(NativeMeshNode owner, ActorRef ref) {
        this.owner = owner;
        this.ref = ref;
    }

    private MemorySegment nodeHandle() {
        return owner.handle();
    }

    @Override
    public ActorRef ref() {
        return ref;
    }

    @Override
    public OperationId joinSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
                                long targetSpotGeneration, List<Message> creationParts,
                                Duration timeout) {
        return owner.joinActorSpot(
            ref,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            creationParts,
            timeout);
    }

    @Override
    public OperationId joinEntrySpot(RoutingId targetNodeRid, List<Message> creationParts,
                                     Duration timeout) {
        return owner.joinActorEntrySpot(ref, targetNodeRid, creationParts, timeout);
    }

    @Override
    public OperationId leaveSpot(long expectedMembershipEpoch, Duration timeout) {
        return owner.leaveActor(ref, expectedMembershipEpoch, timeout);
    }

    @Override
    public void sendTo(ActorRef target, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(target, "target");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment srcRef = ServiceInterop.actorRefToNative(arena, ref);
            MemorySegment tgtRef = ServiceInterop.actorRefToNative(arena, target);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.actorSendToActor(nodeHandle(), srcRef, tgtRef,
                MemorySegment.NULL, array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_actor_send_to_actor");
        }
    }

    @Override
    public OperationId requestTo(ActorRef target, List<Message> parts, SendFlags flags,
                                 Duration timeout) {
        Objects.requireNonNull(target, "target");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment srcRef = ServiceInterop.actorRefToNative(arena, ref);
            MemorySegment tgtRef = ServiceInterop.actorRefToNative(arena, target);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.actorRequestToActor(nodeHandle(), srcRef, tgtRef,
                MemorySegment.NULL, array, n, opid, flags.value(), MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_actor_request_to_actor");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public void sendBoundSession(List<Message> parts, SendFlags flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment refSeg = ServiceInterop.actorRefToNative(arena, ref);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.actorSendBoundSession(nodeHandle(), refSeg, array, n,
                flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_actor_send_bound_session");
        }
    }

    @Override
    public OperationId closeBoundSession(long expectedBindingGeneration, Duration timeout) {
        return owner.closeActorBoundSession(ref, expectedBindingGeneration, timeout);
    }

    @Override
    public OperationId destroy(Duration timeout) {
        return owner.destroyActor(ref, timeout);
    }

    @Override
    public void close() {
        // Actors have no owned native handle; destruction is an explicit
        // operation via destroy(Duration).
    }
}
