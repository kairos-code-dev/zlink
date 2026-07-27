"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkLocationTopologyState = void 0;
var ZLinkLocationTopologyState;
(function (ZLinkLocationTopologyState) {
    ZLinkLocationTopologyState[ZLinkLocationTopologyState["Discovered"] = 1] = "Discovered";
    ZLinkLocationTopologyState[ZLinkLocationTopologyState["Connecting"] = 2] = "Connecting";
    ZLinkLocationTopologyState[ZLinkLocationTopologyState["Ready"] = 3] = "Ready";
    ZLinkLocationTopologyState[ZLinkLocationTopologyState["Lost"] = 4] = "Lost";
    ZLinkLocationTopologyState[ZLinkLocationTopologyState["Error"] = 5] = "Error";
    ZLinkLocationTopologyState[ZLinkLocationTopologyState["Stopped"] = 6] = "Stopped";
})(ZLinkLocationTopologyState || (exports.ZLinkLocationTopologyState = ZLinkLocationTopologyState = {}));
