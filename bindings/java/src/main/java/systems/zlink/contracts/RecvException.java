/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public final class RecvException extends ZlinkException {
    private final RecvResult result;

    public RecvException(RecvResult result) {
        this(result, 0);
    }

    public RecvException(RecvResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public RecvResult getResult() {
        return result;
    }
}
