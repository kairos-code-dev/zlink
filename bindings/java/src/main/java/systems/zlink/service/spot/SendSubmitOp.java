/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.SendFlags;

public interface SendSubmitOp {
    SendSubmitOp message(Message part);
    SendSubmitOp flags(SendFlags flags);
    boolean submit();
}
