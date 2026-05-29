/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface RequestSubmitOperation {
    RequestSubmitOperation message(Message part);
    RequestSubmitOperation timeout(Duration timeout);
    RequestCallbackSubmitOperation flags(SendFlags flags);
    CompletableFuture<List<Message>> submitAsync();
    boolean submit(RequestCallback callback);
}
