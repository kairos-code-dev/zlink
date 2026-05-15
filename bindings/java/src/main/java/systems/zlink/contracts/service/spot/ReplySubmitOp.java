/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.SendFlags;

public interface ReplySubmitOp {
    ReplySubmitOp message(Message part);
    ReplySubmitOp flags(SendFlags flags);
    void submit();
}
