/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.RequestCallback;
import systems.zlink.contracts.SendFlags;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface RequestSubmitOp {
    RequestSubmitOp message(Message part);
    RequestSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    CompletableFuture<List<Message>> submitAsync();
    boolean submit(RequestCallback callback);
}
