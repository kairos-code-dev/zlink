/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

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
