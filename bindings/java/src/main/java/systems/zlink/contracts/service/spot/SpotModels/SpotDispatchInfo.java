/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.eventing.ZlinkTimer;
import systems.zlink.runtime.nativeapi.ContractAccess;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/** The event and context passed to a spot dispatch callback. */
public final class SpotDispatchInfo {
    private final SpotDispatchEvent event;
    private final SpotDispatchSubjectKind subjectKind;
    private final Object subjectState;
    private final ZlinkTimer timer;
    private final String channelName;
    private final List<ActorReceived> actorMessages;

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
                                           List<ActorReceived> actorMessages) {
                return new SpotDispatchInfo(event, subjectKind, subject,
                    actorMessages);
            }

            @Override
            public SpotDispatchInfo create(SpotDispatchEvent event,
                                           SpotDispatchSubjectKind subjectKind,
                                           Object subject,
                                           ZlinkTimer timer,
                                           String channelName,
                                           List<ActorReceived> actorMessages) {
                return new SpotDispatchInfo(event, subjectKind, subject, timer,
                    channelName, actorMessages);
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
                     List<ActorReceived> actorMessages) {
        this(event, subjectKind, subject, null, null, actorMessages);
    }

    SpotDispatchInfo(SpotDispatchEvent event,
                     SpotDispatchSubjectKind subjectKind,
                     Object subject,
                     ZlinkTimer timer,
                     String channelName,
                     List<ActorReceived> actorMessages) {
        this.event = Objects.requireNonNull(event, "event");
        this.subjectKind = Objects.requireNonNull(subjectKind, "subjectKind");
        this.subjectState = subject;
        this.timer = timer;
        this.channelName = channelName;
        this.actorMessages = List.copyOf(
          Objects.requireNonNull(actorMessages, "actorMessages"));
    }

    public SpotDispatchEvent event() {
        return event;
    }

    public SpotDispatchSubjectKind subjectKind() {
        return subjectKind;
    }

    public Optional<ZlinkTimer> timer() {
        return Optional.ofNullable(timer);
    }

    Optional<String> channelName() {
        return Optional.ofNullable(channelName);
    }

    public List<ActorReceived> actorMessages() {
        return actorMessages;
    }

    Object subjectState() {
        return subjectState;
    }
}
