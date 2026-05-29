/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotHandler;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotResult;
import systems.zlink.contracts.service.spot.ActorJoinHandler;
import systems.zlink.contracts.service.spot.ActorJoinResult;
import systems.zlink.contracts.service.spot.ActorLookupHandler;
import systems.zlink.contracts.service.spot.ActorLookupResult;
import systems.zlink.contracts.service.spot.SpotActorLifecycleInfo;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

/**
 * Shared infrastructure for async Actor operation callbacks (reply, join,
 * lookup, lifecycle). Each pending call holds a Java continuation keyed by a
 * 64-bit id; the upcall stubs translate native callback payloads into the
 * registered Java consumer.
 */
public final class ActorRequestCallbacks {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_ACTOR_JOIN_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_ACTOR_JOIN_ENTRY_SPOT_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_ACTOR_LOOKUP_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_ACTOR_LIFECYCLE_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    public static final MemorySegment REPLY_CALLBACK;
    public static final MemorySegment ACTOR_JOIN_CALLBACK;
    public static final MemorySegment ACTOR_JOIN_ENTRY_SPOT_CALLBACK;
    public static final MemorySegment ACTOR_LOOKUP_CALLBACK;
    public static final MemorySegment ACTOR_LIFECYCLE_JOIN_CALLBACK;
    public static final MemorySegment ACTOR_LIFECYCLE_LEAVE_CALLBACK;
    private static final AtomicLong NEXT_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, Pending> PENDING =
      new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, JoinPending> JOIN_PENDING =
      new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, JoinEntrySpotPending>
      JOIN_ENTRY_SPOT_PENDING = new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, LookupPending> LOOKUP_PENDING =
      new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, LifecyclePending> LIFECYCLE_REGS =
      new ConcurrentHashMap<>();

    static {
        try {
            REPLY_CALLBACK = LINKER.upcallStub(MethodHandles.lookup().findStatic(
              ActorRequestCallbacks.class, "handleReplyCallback",
              MethodType.methodType(void.class, int.class, MemorySegment.class,
                long.class, MemorySegment.class)), FD_REPLY_CALLBACK,
              CALLBACK_ARENA);
            ACTOR_JOIN_CALLBACK = LINKER.upcallStub(MethodHandles.lookup().findStatic(
              ActorRequestCallbacks.class, "handleActorJoinCallback",
              MethodType.methodType(void.class, MemorySegment.class,
                MemorySegment.class, long.class, MemorySegment.class)),
              FD_ACTOR_JOIN_CALLBACK, CALLBACK_ARENA);
            ACTOR_JOIN_ENTRY_SPOT_CALLBACK = LINKER.upcallStub(
              MethodHandles.lookup().findStatic(
                ActorRequestCallbacks.class, "handleActorJoinEntrySpotCallback",
                MethodType.methodType(void.class, MemorySegment.class,
                  MemorySegment.class)), FD_ACTOR_JOIN_ENTRY_SPOT_CALLBACK,
              CALLBACK_ARENA);
            ACTOR_LOOKUP_CALLBACK = LINKER.upcallStub(
              MethodHandles.lookup().findStatic(
                ActorRequestCallbacks.class, "handleActorLookupCallback",
                MethodType.methodType(void.class, MemorySegment.class,
                  MemorySegment.class)), FD_ACTOR_LOOKUP_CALLBACK,
              CALLBACK_ARENA);
            ACTOR_LIFECYCLE_JOIN_CALLBACK = LINKER.upcallStub(
              MethodHandles.lookup().findStatic(
                ActorRequestCallbacks.class, "handleActorLifecycleJoin",
                MethodType.methodType(void.class, MemorySegment.class,
                  MemorySegment.class, MemorySegment.class)),
              FD_ACTOR_LIFECYCLE_CALLBACK, CALLBACK_ARENA);
            ACTOR_LIFECYCLE_LEAVE_CALLBACK = LINKER.upcallStub(
              MethodHandles.lookup().findStatic(
                ActorRequestCallbacks.class, "handleActorLifecycleLeave",
                MethodType.methodType(void.class, MemorySegment.class,
                  MemorySegment.class, MemorySegment.class)),
              FD_ACTOR_LIFECYCLE_CALLBACK, CALLBACK_ARENA);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private ActorRequestCallbacks() {
    }

    public static PendingToken register(BiConsumer<RequestResult, List<Message>> callback) {
        long id = NEXT_ID.getAndIncrement();
        CompletableFuture<Void> future = new CompletableFuture<>();
        PENDING.put(id, new Pending(callback, future));
        return new PendingToken(id, future);
    }

    public static JoinPendingToken registerJoin(ActorJoinHandler handler) {
        long id = NEXT_ID.getAndIncrement();
        CompletableFuture<Void> future = new CompletableFuture<>();
        JOIN_PENDING.put(id, new JoinPending(handler, future));
        return new JoinPendingToken(id, future);
    }

    public static JoinEntrySpotPendingToken registerJoinEntrySpot(
      ActorJoinEntrySpotHandler handler) {
        long id = NEXT_ID.getAndIncrement();
        CompletableFuture<Void> future = new CompletableFuture<>();
        JOIN_ENTRY_SPOT_PENDING.put(id,
          new JoinEntrySpotPending(handler, future));
        return new JoinEntrySpotPendingToken(id, future);
    }

    public static LookupPendingToken registerLookup(ActorLookupHandler handler) {
        long id = NEXT_ID.getAndIncrement();
        CompletableFuture<Void> future = new CompletableFuture<>();
        LOOKUP_PENDING.put(id, new LookupPending(handler, future));
        return new LookupPendingToken(id, future);
    }

    public static long registerLifecycle(LifecycleDispatcher onJoin,
                                         LifecycleDispatcher onLeave) {
        long id = NEXT_ID.getAndIncrement();
        LIFECYCLE_REGS.put(id, new LifecyclePending(onJoin, onLeave));
        return id;
    }

    public static void unregisterLifecycle(long id) {
        LIFECYCLE_REGS.remove(id);
    }

    public static void remove(long id) {
        Pending pending = PENDING.remove(id);
        if (pending != null) {
            pending.future().complete(null);
        }
        JoinPending join = JOIN_PENDING.remove(id);
        if (join != null) {
            join.future().complete(null);
        }
        JoinEntrySpotPending entryJoin = JOIN_ENTRY_SPOT_PENDING.remove(id);
        if (entryJoin != null) {
            entryJoin.future().complete(null);
        }
        LookupPending lookup = LOOKUP_PENDING.remove(id);
        if (lookup != null) {
            lookup.future().complete(null);
        }
    }

    public static void await(PendingToken token) {
        try {
            token.future().join();
        } catch (CompletionException ex) {
            if (ex.getCause() instanceof systems.zlink.contracts.errors.ZlinkRequestException request) {
                throw request;
            }
            throw ex;
        }
    }

    @SuppressWarnings("unused")
    private static void handleReplyCallback(int result,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long id = userdata.address();
        Pending pending = PENDING.remove(id);
        if (pending == null) {
            NativeMsg.multipartClose(parts, partCount);
            return;
        }
        try {
            Message[] messages = result == RequestResult.OK.value()
              ? InternalAccess.messageFromOwnedMsgVectorShared(parts, partCount)
              : new Message[0];
            pending.callback().accept(RequestResult.fromValue(result),
              Collections.unmodifiableList(Arrays.asList(messages)));
            pending.future().complete(null);
        } catch (RuntimeException ex) {
            pending.future().completeExceptionally(ex);
            throw ex;
        } finally {
            NativeMsg.multipartClose(parts, partCount);
        }
    }

    @SuppressWarnings("unused")
    private static void handleActorJoinCallback(MemorySegment result,
                                                MemorySegment parts,
                                                long partCount,
                                                MemorySegment userdata) {
        long id = userdata.address();
        JoinPending join = JOIN_PENDING.remove(id);
        if (join == null) {
            // fall back to legacy reply-pending path for callers that still
            // register through register(...)
            int code = result == MemorySegment.NULL
              ? RequestResult.INTERNAL_ERROR.value()
              : result.get(ValueLayout.JAVA_INT, 0);
            handleReplyCallback(code, parts, partCount, userdata);
            return;
        }
        try {
            ActorJoinResult joinResult = result == MemorySegment.NULL
              ? new ActorJoinResult(RequestResult.INTERNAL_ERROR, 0, null,
                  null, 0L, 0)
              : ActorInterop.actorJoinResultFromNative(result);
            Message[] messages = joinResult.result() == RequestResult.OK
              ? InternalAccess.messageFromOwnedMsgVectorShared(parts, partCount)
              : new Message[0];
            join.handler().onJoinResult(joinResult,
              Collections.unmodifiableList(Arrays.asList(messages)));
            join.future().complete(null);
        } catch (RuntimeException ex) {
            join.future().completeExceptionally(ex);
            throw ex;
        } finally {
            NativeMsg.multipartClose(parts, partCount);
        }
    }

    @SuppressWarnings("unused")
    private static void handleActorJoinEntrySpotCallback(MemorySegment result,
                                                        MemorySegment userdata) {
        long id = userdata.address();
        JoinEntrySpotPending join = JOIN_ENTRY_SPOT_PENDING.remove(id);
        if (join == null)
            return;
        try {
            ActorJoinEntrySpotResult joinResult = result == MemorySegment.NULL
              ? new ActorJoinEntrySpotResult(RequestResult.INTERNAL_ERROR,
                  null, null, 0L, 0)
              : ActorInterop.actorJoinEntrySpotResultFromNative(result);
            join.handler().onJoinEntrySpotResult(joinResult);
            join.future().complete(null);
        } catch (RuntimeException ex) {
            join.future().completeExceptionally(ex);
            throw ex;
        }
    }

    @SuppressWarnings("unused")
    private static void handleActorLookupCallback(MemorySegment result,
                                                  MemorySegment userdata) {
        long id = userdata.address();
        LookupPending lookup = LOOKUP_PENDING.remove(id);
        if (lookup == null) {
            return;
        }
        try {
            ActorLookupResult lookupResult = result == MemorySegment.NULL
              ? new ActorLookupResult(RequestResult.INTERNAL_ERROR, null, 0)
              : ActorInterop.actorLookupResultFromNative(result);
            lookup.handler().onLookupResult(lookupResult);
            lookup.future().complete(null);
        } catch (RuntimeException ex) {
            lookup.future().completeExceptionally(ex);
            throw ex;
        }
    }

    @SuppressWarnings("unused")
    private static void handleActorLifecycleJoin(MemorySegment spotHandle,
                                                 MemorySegment infoPtr,
                                                 MemorySegment userdata) {
        dispatchLifecycle(spotHandle, infoPtr, userdata, true);
    }

    @SuppressWarnings("unused")
    private static void handleActorLifecycleLeave(MemorySegment spotHandle,
                                                  MemorySegment infoPtr,
                                                  MemorySegment userdata) {
        dispatchLifecycle(spotHandle, infoPtr, userdata, false);
    }

    private static void dispatchLifecycle(MemorySegment spotHandle,
                                          MemorySegment infoPtr,
                                          MemorySegment userdata,
                                          boolean join) {
        long id = userdata.address();
        LifecyclePending entry = LIFECYCLE_REGS.get(id);
        if (entry == null || infoPtr == MemorySegment.NULL) {
            return;
        }
        LifecycleDispatcher dispatcher = join ? entry.onJoin() : entry.onLeave();
        if (dispatcher == null) {
            return;
        }
        try {
            SpotActorLifecycleInfo info =
              ActorInterop.lifecycleInfoFromNative(infoPtr);
            dispatcher.dispatch(spotHandle, info);
        } catch (RuntimeException ex) {
            // surface through dispatcher path; do not propagate to native
        }
    }

    public record PendingToken(long id, CompletableFuture<Void> future) {
    }

    public record JoinPendingToken(long id, CompletableFuture<Void> future) {
    }

    public record JoinEntrySpotPendingToken(long id,
                                           CompletableFuture<Void> future) {
    }

    public record LookupPendingToken(long id, CompletableFuture<Void> future) {
    }

    private record Pending(BiConsumer<RequestResult, List<Message>> callback,
                           CompletableFuture<Void> future) {
    }

    private record JoinPending(ActorJoinHandler handler,
                               CompletableFuture<Void> future) {
    }

    private record JoinEntrySpotPending(ActorJoinEntrySpotHandler handler,
                                        CompletableFuture<Void> future) {
    }

    private record LookupPending(ActorLookupHandler handler,
                                 CompletableFuture<Void> future) {
    }

    private record LifecyclePending(LifecycleDispatcher onJoin,
                                    LifecycleDispatcher onLeave) {
    }

    @FunctionalInterface
    public interface LifecycleDispatcher {
        void dispatch(MemorySegment spotHandle, SpotActorLifecycleInfo info);
    }
}
