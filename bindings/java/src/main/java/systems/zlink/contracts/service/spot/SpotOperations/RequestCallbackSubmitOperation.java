/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;

public interface RequestCallbackSubmitOperation {
    RequestCallbackSubmitOperation message(Message part);
    RequestCallbackSubmitOperation timeout(Duration timeout);
    RequestCallbackSubmitOperation flags(SendFlags flags);
    boolean submit(RequestCallback callback);
}
