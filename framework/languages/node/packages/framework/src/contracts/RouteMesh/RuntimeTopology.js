"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkTerminationReason = exports.ZLinkTerminationOutcome = exports.ZLinkTerminationIntent = void 0;
var ZLinkTerminationIntent;
(function (ZLinkTerminationIntent) {
    ZLinkTerminationIntent[ZLinkTerminationIntent["Retire"] = 0] = "Retire";
    ZLinkTerminationIntent[ZLinkTerminationIntent["Shutdown"] = 1] = "Shutdown";
})(ZLinkTerminationIntent || (exports.ZLinkTerminationIntent = ZLinkTerminationIntent = {}));
var ZLinkTerminationOutcome;
(function (ZLinkTerminationOutcome) {
    ZLinkTerminationOutcome[ZLinkTerminationOutcome["Stopped"] = 0] = "Stopped";
    ZLinkTerminationOutcome[ZLinkTerminationOutcome["Blocked"] = 1] = "Blocked";
    ZLinkTerminationOutcome[ZLinkTerminationOutcome["ForceStopped"] = 2] = "ForceStopped";
})(ZLinkTerminationOutcome || (exports.ZLinkTerminationOutcome = ZLinkTerminationOutcome = {}));
var ZLinkTerminationReason;
(function (ZLinkTerminationReason) {
    ZLinkTerminationReason[ZLinkTerminationReason["None"] = 0] = "None";
    ZLinkTerminationReason[ZLinkTerminationReason["TargetUnavailable"] = 1] = "TargetUnavailable";
    ZLinkTerminationReason[ZLinkTerminationReason["StoreUnavailable"] = 2] = "StoreUnavailable";
    ZLinkTerminationReason[ZLinkTerminationReason["RelocationDisabled"] = 3] = "RelocationDisabled";
    ZLinkTerminationReason[ZLinkTerminationReason["StateIncompatible"] = 4] = "StateIncompatible";
    ZLinkTerminationReason[ZLinkTerminationReason["DeadlineExceeded"] = 5] = "DeadlineExceeded";
    ZLinkTerminationReason[ZLinkTerminationReason["RelocationFailed"] = 6] = "RelocationFailed";
    ZLinkTerminationReason[ZLinkTerminationReason["TeardownFailed"] = 7] = "TeardownFailed";
    ZLinkTerminationReason[ZLinkTerminationReason["RuntimeNotReady"] = 8] = "RuntimeNotReady";
    ZLinkTerminationReason[ZLinkTerminationReason["ManualTopologyUnsupported"] = 9] = "ManualTopologyUnsupported";
})(ZLinkTerminationReason || (exports.ZLinkTerminationReason = ZLinkTerminationReason = {}));
