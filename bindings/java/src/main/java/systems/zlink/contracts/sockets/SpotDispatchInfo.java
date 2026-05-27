/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.service.spot.ActorPart;
import systems.zlink.contracts.eventing.Timer;
import systems.zlink.runtime.nativeapi.InternalAccess;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

public final class SpotDispatchInfo {
    private final SpotDispatchEvent event;
    private final SpotDispatchSubjectKind subjectKind;
    private final MemorySegment subject;
    private final Timer timer;
    private final String channelName;
    private final List<ActorPart> actorParts;

    static {
        InternalAccess.register(new InternalAccess.SpotDispatchInfoAccess() {
            @Override
            public MemorySegment subject(SpotDispatchInfo info) {
                return info.subject();
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           MemorySegment subject) {
                return new SpotDispatchInfo(event, subjectKind, subject);
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           MemorySegment subject,
                                           List<ActorPart> actorParts) {
                return new SpotDispatchInfo(event, subjectKind, subject,
                    actorParts);
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           MemorySegment subject,
                                           Timer timer,
                                           String channelName,
                                           List<ActorPart> actorParts) {
                return new SpotDispatchInfo(event, subjectKind, subject, timer,
                    channelName, actorParts);
            }
        });
    }

    SpotDispatchInfo(SpotDispatchEvent event,
                     SpotDispatchSubjectKind subjectKind,
                     MemorySegment subject) {
        this(event, subjectKind, subject, null, null, List.of());
    }

    SpotDispatchInfo(SpotDispatchEvent event,
                     SpotDispatchSubjectKind subjectKind,
                     MemorySegment subject,
                     List<ActorPart> actorParts) {
        this(event, subjectKind, subject, null, null, actorParts);
    }

    SpotDispatchInfo(SpotDispatchEvent event,
                     SpotDispatchSubjectKind subjectKind,
                     MemorySegment subject,
                     Timer timer,
                     String channelName,
                     List<ActorPart> actorParts) {
        this.event = Objects.requireNonNull(event, "event");
        this.subjectKind = Objects.requireNonNull(subjectKind, "subjectKind");
        this.subject = subject == null ? MemorySegment.NULL : subject;
        this.timer = timer;
        this.channelName = channelName;
        this.actorParts = List.copyOf(
          Objects.requireNonNull(actorParts, "actorParts"));
    }

    public SpotDispatchEvent event() {
        return event;
    }

    public SpotDispatchSubjectKind subjectKind() {
        return subjectKind;
    }

    public Optional<Timer> timer() {
        return Optional.ofNullable(timer);
    }

    Optional<String> channelName() {
        return Optional.ofNullable(channelName);
    }

    public List<ActorPart> actorParts() {
        return actorParts;
    }

    MemorySegment subject() {
        return subject;
    }
}
