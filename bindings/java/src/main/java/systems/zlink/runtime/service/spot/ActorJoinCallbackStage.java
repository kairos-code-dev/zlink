/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.time.Duration;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinCallbackSubmitOperation;
import systems.zlink.contracts.service.spot.ActorJoinHandler;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.sockets.SendFlags;

final class ActorJoinCallbackStage implements ActorJoinCallbackSubmitOperation {
    private final ActorJoinSubmitOperation builder;

    ActorJoinCallbackStage(ActorJoinSubmitOperation builder) {
        this.builder = builder;
    }

    @Override
    public ActorJoinCallbackSubmitOperation message(Message part) {
        builder.message(part);
        return this;
    }

    @Override
    public ActorJoinCallbackSubmitOperation timeout(Duration timeout) {
        builder.timeout(timeout);
        return this;
    }

    @Override
    public ActorJoinCallbackSubmitOperation flags(SendFlags flags) {
        builder.flags(flags);
        return this;
    }

    @Override
    public boolean submit(ActorJoinHandler callback) {
        return builder.submit(callback);
    }
}
