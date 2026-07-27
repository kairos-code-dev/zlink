"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkFrameworkTerminationReason = exports.ZLinkFrameworkTerminationOutcome = exports.ZLinkFrameworkRelocationReason = exports.ZLinkFrameworkRelocationMode = exports.ZLinkFrameworkRelocationOutcome = void 0;
var ZLinkFrameworkRelocationOutcome;
(function (ZLinkFrameworkRelocationOutcome) {
    ZLinkFrameworkRelocationOutcome[ZLinkFrameworkRelocationOutcome["Relocated"] = 0] = "Relocated";
    ZLinkFrameworkRelocationOutcome[ZLinkFrameworkRelocationOutcome["Blocked"] = 1] = "Blocked";
})(ZLinkFrameworkRelocationOutcome || (exports.ZLinkFrameworkRelocationOutcome = ZLinkFrameworkRelocationOutcome = {}));
var ZLinkFrameworkRelocationMode;
(function (ZLinkFrameworkRelocationMode) {
    ZLinkFrameworkRelocationMode[ZLinkFrameworkRelocationMode["PlannedMaintenance"] = 0] = "PlannedMaintenance";
    ZLinkFrameworkRelocationMode[ZLinkFrameworkRelocationMode["RollingUpdate"] = 1] = "RollingUpdate";
})(ZLinkFrameworkRelocationMode || (exports.ZLinkFrameworkRelocationMode = ZLinkFrameworkRelocationMode = {}));
var ZLinkFrameworkRelocationReason;
(function (ZLinkFrameworkRelocationReason) {
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["None"] = 0] = "None";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["TargetUnavailable"] = 1] = "TargetUnavailable";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["StoreUnavailable"] = 2] = "StoreUnavailable";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["RelocationDisabled"] = 3] = "RelocationDisabled";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["StateIncompatible"] = 4] = "StateIncompatible";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["DeadlineExceeded"] = 5] = "DeadlineExceeded";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["RelocationFailed"] = 6] = "RelocationFailed";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["RuntimeNotReady"] = 7] = "RuntimeNotReady";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["ManualTopologyUnsupported"] = 8] = "ManualTopologyUnsupported";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["ShutdownRequested"] = 9] = "ShutdownRequested";
    ZLinkFrameworkRelocationReason[ZLinkFrameworkRelocationReason["OperationInProgress"] = 10] = "OperationInProgress";
})(ZLinkFrameworkRelocationReason || (exports.ZLinkFrameworkRelocationReason = ZLinkFrameworkRelocationReason = {}));
var ZLinkFrameworkTerminationOutcome;
(function (ZLinkFrameworkTerminationOutcome) {
    ZLinkFrameworkTerminationOutcome[ZLinkFrameworkTerminationOutcome["Stopped"] = 0] = "Stopped";
    ZLinkFrameworkTerminationOutcome[ZLinkFrameworkTerminationOutcome["ForceStopped"] = 1] = "ForceStopped";
})(ZLinkFrameworkTerminationOutcome || (exports.ZLinkFrameworkTerminationOutcome = ZLinkFrameworkTerminationOutcome = {}));
var ZLinkFrameworkTerminationReason;
(function (ZLinkFrameworkTerminationReason) {
    ZLinkFrameworkTerminationReason[ZLinkFrameworkTerminationReason["None"] = 0] = "None";
    ZLinkFrameworkTerminationReason[ZLinkFrameworkTerminationReason["DeadlineExceeded"] = 1] = "DeadlineExceeded";
    ZLinkFrameworkTerminationReason[ZLinkFrameworkTerminationReason["TeardownFailed"] = 2] = "TeardownFailed";
})(ZLinkFrameworkTerminationReason || (exports.ZLinkFrameworkTerminationReason = ZLinkFrameworkTerminationReason = {}));
