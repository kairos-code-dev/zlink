/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.StreamSessionBinding;
import systems.zlink.contracts.service.spot.StreamSessionService;
import systems.zlink.contracts.service.spot.StreamSessionStatus;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public final class NativeStreamSessionService implements StreamSessionService {
    private MemorySegment handle;

    private NativeStreamSessionService(MemorySegment handle) {
        this.handle = handle;
    }

    static NativeStreamSessionService create(NativeMeshNode node, StreamSocket stream) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(stream, "stream");
        MemorySegment svc = NativeServiceSymbols.streamSessionNew(node.handle(),
            InternalAccess.socketHandle(stream));
        if (svc == null || svc.address() == 0) {
            throw systems.zlink.contracts.errors.ZlinkException.fromLastError(
                systems.zlink.contracts.errors.ErrorCategory.CONFIG);
        }
        return new NativeStreamSessionService(svc);
    }

    @Override
    public void start() {
        MeshCalls.configOk(NativeServiceSymbols.streamSessionStart(handle));
    }

    @Override
    public void shutdown(Duration timeout) {
        MeshCalls.requestOk(NativeServiceSymbols.streamSessionShutdown(handle,
            MeshCalls.timeout(timeout)));
    }

    @Override
    public StreamSessionStatus status() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = ServiceInterop.allocStamped(arena,
                ServiceLayouts.STREAM_SESSION_STATUS);
            MeshCalls.configOk(NativeServiceSymbols.streamSessionStatus(handle, out));
            return ServiceInterop.streamSessionStatusFromNative(out);
        }
    }

    @Override
    public OperationId bindActor(RoutingId sessionRid, ActorRef actor, Duration timeout) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, sessionRid);
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.streamSessionBindActor(handle, rid, ref, opid,
                MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, MemorySegment.NULL, 0, "zlink_stream_session_bind_actor");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public OperationId unbindActor(RoutingId sessionRid, ActorRef actor,
                                   long expectedBindingGeneration, Duration timeout) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, sessionRid);
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.streamSessionUnbindActor(handle, rid, ref,
                expectedBindingGeneration, opid, MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, MemorySegment.NULL, 0, "zlink_stream_session_unbind_actor");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public List<StreamSessionBinding> bindings(RoutingId sessionRid) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, sessionRid);
            MemorySegment countSeg = arena.allocate(ValueLayout.JAVA_LONG);
            countSeg.set(ValueLayout.JAVA_LONG, 0, 0L);
            NativeServiceSymbols.streamSessionBindings(handle, rid, MemorySegment.NULL, countSeg);
            long capacity = countSeg.get(ValueLayout.JAVA_LONG, 0);
            if (capacity == 0) {
                return List.of();
            }
            long stride = ServiceLayouts.STREAM_SESSION_BINDING.byteSize();
            MemorySegment entries = arena.allocate(stride * capacity);
            for (long i = 0; i < capacity; i++) {
                ServiceInterop.stampHeader(entries.asSlice(i * stride, stride),
                    ServiceLayouts.STREAM_SESSION_BINDING);
            }
            countSeg.set(ValueLayout.JAVA_LONG, 0, capacity);
            MeshCalls.configOk(NativeServiceSymbols.streamSessionBindings(handle, rid, entries,
                countSeg));
            long n = countSeg.get(ValueLayout.JAVA_LONG, 0);
            List<StreamSessionBinding> out = new ArrayList<>((int) n);
            for (long i = 0; i < n; i++) {
                out.add(ServiceInterop.streamSessionBindingFromNative(
                    entries.asSlice(i * stride, stride)));
            }
            return out;
        }
    }

    @Override
    public void sendToActor(RoutingId sessionRid, ActorRef actor, List<Message> parts,
                            SendFlags flags) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, sessionRid);
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.streamSessionSendToActor(handle, rid, ref,
                MemorySegment.NULL, array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_stream_session_send_to_actor");
        }
    }

    @Override
    public OperationId requestToActor(RoutingId sessionRid, ActorRef actor, List<Message> parts,
                                      SendFlags flags, Duration timeout) {
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, sessionRid);
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.streamSessionRequestToActor(handle, rid, ref,
                MemorySegment.NULL, array, n, opid, flags.value(), MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_stream_session_request_to_actor");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0) {
            return;
        }
        NativeServiceSymbols.streamSessionDestroy(handle);
        handle = MemorySegment.NULL;
    }
}
