/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.contracts.errors.HandlerResult;
import systems.zlink.contracts.errors.ZlinkHandlerException;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotDispatchEventHandler;
import systems.zlink.contracts.service.spot.SpotDispatchInfo;
import systems.zlink.contracts.service.spot.SpotDispatchSubjectKind;
import systems.zlink.runtime.nativeapi.ContractAccess;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeCallbackSupport;
import systems.zlink.runtime.nativeapi.NativeLayouts;

final class SpotDispatchSupport implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_DISPATCH_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS);
    private static final Arena DISPATCH_CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment DISPATCH_CALLBACK =
      LINKER.upcallStub(
        callbackHandle("handleDispatchEventCallback",
          MethodType.methodType(void.class, MemorySegment.class,
            MemorySegment.class, MemorySegment.class)),
        FD_DISPATCH_HANDLER, DISPATCH_CALLBACK_ARENA);
    private static final AtomicLong NEXT_CALLBACK_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, SpotDispatchSupport> RECEIVERS =
      new ConcurrentHashMap<>();

    private final Spot spot;
    private SpotDispatchEventHandler dispatchEventHandler;
    private long dispatchCallbackId;
    private final NativeCallbackSupport callbacks =
        new NativeCallbackSupport("zlink-spot-dispatch-callback");

    SpotDispatchSupport(Spot spot) {
        this.spot = Objects.requireNonNull(spot, "spot");
    }

    void setDispatchHandler(SpotDispatchEventHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        releaseDispatchEventHandlerSlot();
        long callbackId = NEXT_CALLBACK_ID.getAndIncrement();
        dispatchEventHandler = handler;
        dispatchCallbackId = callbackId;
        RECEIVERS.put(callbackId, this);
        int rc = Native.spotDispatchEventHandler(handle(), DISPATCH_CALLBACK,
          MemorySegment.ofAddress(callbackId));
        if (rc != 0) {
            RECEIVERS.remove(callbackId, this);
            dispatchCallbackId = 0L;
            dispatchEventHandler = null;
            throw new ZlinkHandlerException(HandlerResult.fromValue(rc),
              Native.errno());
        }
    }

    void ensureNoCallbackFailure() {
        callbacks.ensureNoFailure();
    }

    @Override
    public void close() {
        releaseDispatchEventHandlerSlot();
        callbacks.close();
    }

    private static void handleDispatchEventCallback(MemorySegment spotHandle,
                                                    MemorySegment info,
                                                    MemorySegment userdata) {
        if (userdata == null || userdata.address() == 0L) {
            return;
        }
        SpotDispatchSupport receiver = RECEIVERS.get(userdata.address());
        if (receiver == null) {
            return;
        }
        receiver.handleDispatchEventCallbackImpl(spotHandle, info);
    }

    private void handleDispatchEventCallbackImpl(MemorySegment spotHandle,
                                                 MemorySegment info) {
        SpotDispatchEventHandler handler = dispatchEventHandler;
        if (handler == null) {
            return;
        }
        try {
            SpotDispatchInfo dispatchInfo = decodeDispatchInfo(info);
            if (dispatchInfo == null) {
                return;
            }
            dispatchEvent(handler, dispatchInfo);
        } catch (RuntimeException ex) {
            callbacks.recordFailure(ex);
        } catch (Error ex) {
            callbacks.recordFailure(new RuntimeException(
                "spot dispatch callback failed", ex));
        }
    }

    private void dispatchEvent(SpotDispatchEventHandler handler,
                               SpotDispatchInfo info) {
        InternalAccess.enterCallback();
        try {
            handler.onEvent(info);
        } catch (RuntimeException ex) {
            callbacks.recordFailure(ex);
        } finally {
            InternalAccess.leaveCallback();
        }
    }

    private SpotDispatchInfo decodeDispatchInfo(MemorySegment info) {
        if (info == null || info.address() == 0) {
            return null;
        }
        info = info.reinterpret(NativeLayouts.SPOT_DISPATCH_INFO_LAYOUT.byteSize());
        SpotDispatchEvent event = EnumCodecs.spotDispatchEventFromValue(info.get(
          ValueLayout.JAVA_INT, NativeLayouts.SPOT_DISPATCH_INFO_EVENT_OFFSET));
        SpotDispatchSubjectKind subjectKind = EnumCodecs.spotDispatchSubjectKindFromValue(
          info.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_DISPATCH_INFO_SUBJECT_KIND_OFFSET));
        if (event == null || subjectKind == null) {
            return null;
        }
        MemorySegment subject = info.get(ValueLayout.ADDRESS,
          NativeLayouts.SPOT_DISPATCH_INFO_SUBJECT_OFFSET);
        if (event == SpotDispatchEvent.ACTOR_READABLE
            && subjectKind == SpotDispatchSubjectKind.ACTOR
            && subject != null && subject.address() != 0) {
            return ContractAccess.spotDispatchInfo(event, subjectKind, subject,
              drainActorReceiveds(subject));
        }
        if (event == SpotDispatchEvent.TIMER_READABLE
            && subjectKind == SpotDispatchSubjectKind.TIMER
            && subject != null && subject.address() != 0) {
            return ContractAccess.spotDispatchInfo(event, subjectKind, subject,
              InternalAccess.timerFromBorrowedHandle(subject), null, List.of());
        }
        return ContractAccess.spotDispatchInfo(event, subjectKind, subject);
    }

    private List<ActorReceived> drainActorReceiveds(MemorySegment actor) {
        MemorySegment node = InternalAccess.spotOwnerNodeHandle(spot);
        if (node == null || node.address() == 0) {
            return List.of();
        }
        ArrayList<ActorReceived> parts = new ArrayList<>();
        try (Arena arena = Arena.ofConfined()) {
            while (true) {
                MemorySegment infoOut = arena.allocate(
                  NativeLayouts.ACTOR_RECV_INFO_LAYOUT);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                Message message = new Message();
                boolean success = false;
                try {
                    int rc = Native.spotNodeActorRecvPart(node, actor, infoOut,
                      InternalAccess.messageNativeHandle(message), hasMoreOut,
                      RecvFlags.DONT_WAIT.value());
                    if (rc != 0) {
                        message.close();
                        if (rc == RecvResult.NO_DATA.value()) {
                            break;
                        }
                        throw new ZlinkRecvException(RecvResult.fromValue(rc));
                    }
                    boolean hasMore =
                      hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(message, hasMore);
                    success = true;
                    parts.add(new ActorReceived(
                      ActorInterop.actorRecvInfoFromNative(infoOut), message,
                      hasMore));
                } finally {
                    if (!success) {
                        try {
                            message.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }
            }
        }
        return List.copyOf(parts);
    }

    private void releaseDispatchEventHandlerSlot() {
        long callbackId = dispatchCallbackId;
        if (callbackId != 0L) {
            RECEIVERS.remove(callbackId);
            dispatchCallbackId = 0L;
        }
        dispatchEventHandler = null;
    }

    private MemorySegment handle() {
        return InternalAccess.spotHandle(spot);
    }

    private void ensureOpen() {
        MemorySegment handle = handle();
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("spot is closed");
        }
        ensureNoCallbackFailure();
    }

    private static MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findStatic(
              SpotDispatchSupport.class, name, type);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }
}
