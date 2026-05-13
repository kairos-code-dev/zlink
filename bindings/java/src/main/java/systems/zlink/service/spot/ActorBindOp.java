/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface ActorBindOp {
    ActorBindOp timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();
    boolean submit(ReplyHandler callback);
}
