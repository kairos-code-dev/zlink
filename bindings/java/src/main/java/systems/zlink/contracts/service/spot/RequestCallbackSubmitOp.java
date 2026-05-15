/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.RequestCallback;
import systems.zlink.contracts.SendFlags;
import java.time.Duration;

public interface RequestCallbackSubmitOp {
    RequestCallbackSubmitOp message(Message part);
    RequestCallbackSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    boolean submit(RequestCallback callback);
}
