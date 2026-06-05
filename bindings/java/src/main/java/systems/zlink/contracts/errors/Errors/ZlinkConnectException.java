/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


/** Thrown when connecting a socket to an endpoint fails. */
public final class ZlinkConnectException extends ZlinkException {
    private final ConnectResult result;

    public ZlinkConnectException(ConnectResult result) {
        this(result, 0);
    }

    public ZlinkConnectException(ConnectResult result, int nativeErrno) {
        super(result.value(), nativeErrno);
        this.result = result;
    }

    public ConnectResult getResult() {
        return result;
    }
}
