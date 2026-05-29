/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface ActorUnbindOperation {
    ActorUnbindOperation timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();
    boolean submit(ReplyHandler callback);
}
