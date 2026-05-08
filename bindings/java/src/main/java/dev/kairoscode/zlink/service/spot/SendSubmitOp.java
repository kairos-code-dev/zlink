/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlags;

public interface SendSubmitOp {
    SendSubmitOp message(Message part);
    SendSubmitOp flags(SendFlags flags);
    boolean submit();
}
