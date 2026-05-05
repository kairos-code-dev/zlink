/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.ActorInterop;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.service.spot.ActorPart;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.Arena;
import java.lang.foreign.ValueLayout;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicInteger;

public record SpotDispatchInfo(SpotDispatchEvent event,
                               SpotDispatchSubjectKind subjectKind,
                               MemorySegment subject,
                               List<ActorPart> preloadedActorParts,
                               AtomicInteger actorPartCursor) {
    public SpotDispatchInfo(SpotDispatchEvent event,
                            SpotDispatchSubjectKind subjectKind,
                            MemorySegment subject) {
        this(event, subjectKind, subject, List.of(), new AtomicInteger());
    }

    public ActorPart recvActorPart(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (event != SpotDispatchEvent.ACTOR_READABLE
            || subjectKind != SpotDispatchSubjectKind.ACTOR
            || subject == null || subject.address() == 0) {
            throw new RecvException(RecvResult.NOT_SUPPORTED);
        }
        int index = actorPartCursor.getAndIncrement();
        if (index < preloadedActorParts.size()) {
            return preloadedActorParts.get(index);
        }
        if (!preloadedActorParts.isEmpty()) {
            if (flags == RecvFlags.DONT_WAIT) {
                return null;
            }
            throw new RecvException(RecvResult.NO_DATA);
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment infoOut = arena.allocate(
              NativeLayouts.ACTOR_RECV_INFO_LAYOUT);
            MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
            Message message = new Message();
            boolean success = false;
            try {
                int rc = Native.actorRecvPart(subject, infoOut,
                  InternalAccess.messageNativeHandle(message), hasMoreOut,
                  flags.value());
                if (rc != 0) {
                    if (flags == RecvFlags.DONT_WAIT
                        && rc == RecvResult.NO_DATA.value()) {
                        return null;
                    }
                    throw new RecvException(RecvResult.fromValue(rc));
                }
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(message, hasMore);
                success = true;
                return new ActorPart(ActorInterop.actorRecvInfoFromNative(
                  infoOut), message, hasMore);
            } finally {
                if (!success) {
                    message.close();
                }
            }
        }
    }

    public ActorPart recvActorPart() {
        return recvActorPart(RecvFlags.NONE);
    }
}
