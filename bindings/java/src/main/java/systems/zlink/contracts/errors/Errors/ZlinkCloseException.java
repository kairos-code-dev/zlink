/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


public final class ZlinkCloseException extends ZlinkException {
    private final CloseResult result;

    public ZlinkCloseException(CloseResult result) {
        this(result, 0);
    }

    public ZlinkCloseException(CloseResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public CloseResult getResult() {
        return result;
    }
}
