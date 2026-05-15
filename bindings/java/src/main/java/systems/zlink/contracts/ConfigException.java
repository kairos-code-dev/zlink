/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public final class ConfigException extends ZlinkException {
    private final ConfigResult result;

    public ConfigException(ConfigResult result) {
        this(result, 0);
    }

    public ConfigException(ConfigResult result, int internalErrno) {
        this(result, internalErrno, result.name());
    }

    ConfigException(ConfigResult result, int internalErrno, String message) {
        super(message, result.value(), internalErrno);
        this.result = result;
    }

    public ConfigResult getResult() {
        return result;
    }
}
