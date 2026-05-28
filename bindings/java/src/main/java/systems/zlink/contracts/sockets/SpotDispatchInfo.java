/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.service.spot.ActorPart;
import systems.zlink.contracts.eventing.Timer;
import systems.zlink.contracts.internal.ContractAccess;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

public final class SpotDispatchInfo {
    private final SpotDispatchEvent event;
    private final SpotDispatchSubjectKind subjectKind;
    private final Object subjectState;
    private final Timer timer;
    private final String channelName;
    private final List<ActorPart> actorParts;

    static {
        ContractAccess.register(new ContractAccess.SpotDispatchInfoAccess() {
            @Override
            public Object subjectState(SpotDispatchInfo info) {
                return info.subjectState();
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           Object subject) {
                return new SpotDispatchInfo(event, subjectKind, subject);
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           Object subject,
                                           List<ActorPart> actorParts) {
                return new SpotDispatchInfo(event, subjectKind, subject,
                    actorParts);
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           Object subject,
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
                     Object subject) {
        this(event, subjectKind, subject, null, null, List.of());
    }

    SpotDispatchInfo(SpotDispatchEvent event,
                     SpotDispatchSubjectKind subjectKind,
                     Object subject,
                     List<ActorPart> actorParts) {
        this(event, subjectKind, subject, null, null, actorParts);
    }

    SpotDispatchInfo(SpotDispatchEvent event,
                     SpotDispatchSubjectKind subjectKind,
                     Object subject,
                     Timer timer,
                     String channelName,
                     List<ActorPart> actorParts) {
        this.event = Objects.requireNonNull(event, "event");
        this.subjectKind = Objects.requireNonNull(subjectKind, "subjectKind");
        this.subjectState = subject;
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

    Object subjectState() {
        return subjectState;
    }
}
