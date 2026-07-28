"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkTopologyState = exports.ZLinkRequestFailureError = void 0;
class ZLinkRequestFailureError extends Error {
    reason;
    constructor(reason, message, cause) {
        super(message, { cause });
        this.name = 'ZLinkRequestFailureError';
        this.reason = reason;
    }
}
exports.ZLinkRequestFailureError = ZLinkRequestFailureError;
var ZLinkTopologyState;
(function (ZLinkTopologyState) {
    ZLinkTopologyState[ZLinkTopologyState["Starting"] = 0] = "Starting";
    ZLinkTopologyState[ZLinkTopologyState["Ready"] = 1] = "Ready";
    ZLinkTopologyState[ZLinkTopologyState["Degraded"] = 2] = "Degraded";
    ZLinkTopologyState[ZLinkTopologyState["Stopping"] = 3] = "Stopping";
    ZLinkTopologyState[ZLinkTopologyState["Stopped"] = 4] = "Stopped";
    ZLinkTopologyState[ZLinkTopologyState["Failed"] = 5] = "Failed";
})(ZLinkTopologyState || (exports.ZLinkTopologyState = ZLinkTopologyState = {}));
