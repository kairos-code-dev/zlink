/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


/** Thrown when reading or applying a configuration option fails. */
public final class ZlinkConfigException extends ZlinkException {
    private final ConfigResult result;

    public ZlinkConfigException(ConfigResult result) {
        this(result, 0);
    }

    public ZlinkConfigException(ConfigResult result, int internalErrno) {
        this(result, internalErrno, result.name());
    }

    ZlinkConfigException(ConfigResult result, int internalErrno, String message) {
        super(message, result.value(), internalErrno);
        this.result = result;
    }

    public ConfigResult getResult() {
        return result;
    }
}
