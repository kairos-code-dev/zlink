"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkSpotKind = void 0;
exports.zlinkSpotKindToWire = zlinkSpotKindToWire;
exports.zlinkSpotKindFromWire = zlinkSpotKindFromWire;
var ZLinkSpotKind;
(function (ZLinkSpotKind) {
    ZLinkSpotKind["Invalid"] = "invalid";
    ZLinkSpotKind["Entry"] = "entry";
    ZLinkSpotKind["User"] = "user";
    ZLinkSpotKind["Instance"] = "instance";
})(ZLinkSpotKind || (exports.ZLinkSpotKind = ZLinkSpotKind = {}));
function zlinkSpotKindToWire(kind) {
    switch (kind) {
        case ZLinkSpotKind.Entry: return 1;
        case ZLinkSpotKind.User: return 2;
        case ZLinkSpotKind.Instance: return 3;
        default: return 0;
    }
}
function zlinkSpotKindFromWire(value) {
    switch (value) {
        case 1: return ZLinkSpotKind.Entry;
        case 2: return ZLinkSpotKind.User;
        case 3: return ZLinkSpotKind.Instance;
        default: return ZLinkSpotKind.Invalid;
    }
}
