/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.SendFlags;

public interface SendSubmitOp {
    SendSubmitOp message(Message part);
    SendSubmitOp flags(SendFlags flags);
    boolean submit();
}
