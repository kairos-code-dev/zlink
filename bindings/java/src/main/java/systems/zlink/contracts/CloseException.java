/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


public final class CloseException extends ZlinkException {
    private final CloseResult result;

    public CloseException(CloseResult result) {
        this(result, 0);
    }

    public CloseException(CloseResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public CloseResult getResult() {
        return result;
    }
}
