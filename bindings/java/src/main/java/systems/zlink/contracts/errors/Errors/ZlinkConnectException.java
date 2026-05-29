/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


public final class ZlinkConnectException extends ZlinkException {
    private final ConnectResult result;

    public ZlinkConnectException(ConnectResult result) {
        this(result, 0);
    }

    public ZlinkConnectException(ConnectResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public ConnectResult getResult() {
        return result;
    }
}
