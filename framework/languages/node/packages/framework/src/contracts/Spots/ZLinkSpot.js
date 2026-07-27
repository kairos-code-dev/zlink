"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkSpotCloseReason = void 0;
var ZLinkSpotCloseReason;
(function (ZLinkSpotCloseReason) {
    ZLinkSpotCloseReason[ZLinkSpotCloseReason["ExplicitClose"] = 0] = "ExplicitClose";
    ZLinkSpotCloseReason[ZLinkSpotCloseReason["HostShutdown"] = 1] = "HostShutdown";
    ZLinkSpotCloseReason[ZLinkSpotCloseReason["RelocationOut"] = 2] = "RelocationOut";
})(ZLinkSpotCloseReason || (exports.ZLinkSpotCloseReason = ZLinkSpotCloseReason = {}));
