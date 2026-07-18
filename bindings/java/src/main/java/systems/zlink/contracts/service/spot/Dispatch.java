/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.runtime.nativeapi.InternalAccess;
import java.util.List;
import java.util.Objects;

/** Free functions for replying to received requests and actor joins. */
public final class Dispatch {
    private Dispatch() {
    }

    /** Sends a reply for the request identified by the given token. */
    public static void reply(ReplyToken token, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(token, "token");
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        InternalAccess.dispatchReply(token.opaque(), parts, flags.value());
    }

    /** Sends an actor-join reply for the join identified by the given token. */
    public static void actorJoinReply(ReplyToken token, ActorJoinDecision decision,
                                      List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(token, "token");
        Objects.requireNonNull(decision, "decision");
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        InternalAccess.dispatchActorJoinReply(token.opaque(), decision.value(), parts,
            flags.value());
    }
}
