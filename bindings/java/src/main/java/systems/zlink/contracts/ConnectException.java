/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public final class ConnectException extends ZlinkException {
    private final ConnectResult result;

    public ConnectException(ConnectResult result) {
        this(result, 0);
    }

    public ConnectException(ConnectResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public ConnectResult getResult() {
        return result;
    }
}
