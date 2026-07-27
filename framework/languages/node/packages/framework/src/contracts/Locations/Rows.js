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
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Relocating"] = 2] = "Relocating";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Relocated"] = 3] = "Relocated";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Draining"] = 4] = "Draining";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Stopped"] = 5] = "Stopped";
    ZLinkFrameworkRuntimeState[ZLinkFrameworkRuntimeState["Error"] = 6] = "Error";
})(ZLinkFrameworkRuntimeState || (exports.ZLinkFrameworkRuntimeState = ZLinkFrameworkRuntimeState = {}));
