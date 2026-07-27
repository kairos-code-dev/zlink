"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkFrameworkRuntimeState = exports.ZLinkObjectRole = void 0;
var ZLinkObjectRole;
(function (ZLinkObjectRole) {
    ZLinkObjectRole["None"] = "none";
    ZLinkObjectRole["Client"] = "client";
    ZLinkObjectRole["Server"] = "server";
})(ZLinkObjectRole || (exports.ZLinkObjectRole = ZLinkObjectRole = {}));
var ZLinkFrameworkRuntimeState;
(function (ZLinkFrameworkRuntimeState) {
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Preparing"] = 0] = "Preparing";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Serving"] = 1] = "Serving";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Retiring"] = 2] = "Retiring";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Draining"] = 3] = "Draining";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Stopped"] = 4] = "Stopped";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Error"] = 5] = "Error";
})(ZLinkFrameworkRuntimeState || (exports.ZLinkFrameworkRuntimeState = ZLinkFrameworkRuntimeState = {}));
