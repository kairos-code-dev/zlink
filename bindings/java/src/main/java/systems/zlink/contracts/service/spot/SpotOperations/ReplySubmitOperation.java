/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
public interface ReplySubmitOperation {
    ReplySubmitOperation message(Message part);
    ReplySubmitOperation flags(SendFlags flags);
    void submit();
}
