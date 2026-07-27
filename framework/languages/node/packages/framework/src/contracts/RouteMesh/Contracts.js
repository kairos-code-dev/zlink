"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkMeshNodeState = exports.ZLinkRequestFailureError = void 0;
class ZLinkRequestFailureError extends Error {
    reason;
    constructor(reason, message, cause) {
        super(message, { cause });
        this.name = 'ZLinkRequestFailureError';
        this.reason = reason;
    }
}
exports.ZLinkRequestFailureError = ZLinkRequestFailureError;
var ZLinkMeshNodeState;
(function (ZLinkMeshNodeState) {
    ZLinkMeshNodeState[ZLinkMeshNodeState["Starting"] = 0] = "Starting";
    ZLinkMeshNodeState[ZLinkMeshNodeState["Serving"] = 1] = "Serving";
    ZLinkMeshNodeState[ZLinkMeshNodeState["Draining"] = 2] = "Draining";
    ZLinkMeshNodeState[ZLinkMeshNodeState["Drained"] = 3] = "Drained";
    ZLinkMeshNodeState[ZLinkMeshNodeState["ForceStopping"] = 4] = "ForceStopping";
    ZLinkMeshNodeState[ZLinkMeshNodeState["Stopped"] = 5] = "Stopped";
    ZLinkMeshNodeState[ZLinkMeshNodeState["Faulted"] = 6] = "Faulted";
})(ZLinkMeshNodeState || (exports.ZLinkMeshNodeState = ZLinkMeshNodeState = {}));
