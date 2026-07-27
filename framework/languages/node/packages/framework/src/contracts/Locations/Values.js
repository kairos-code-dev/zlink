"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkLocationKind = exports.ZLinkRouteKind = exports.ZLinkLocationRole = exports.ZLinkLocationAutoConnectType = void 0;
var ZLinkLocationAutoConnectType;
(function (ZLinkLocationAutoConnectType) {
    ZLinkLocationAutoConnectType[ZLinkLocationAutoConnectType["Invalid"] = 0] = "Invalid";
    ZLinkLocationAutoConnectType[ZLinkLocationAutoConnectType["RouteMesh"] = 1] = "RouteMesh";
    ZLinkLocationAutoConnectType[ZLinkLocationAutoConnectType["Fanout"] = 2] = "Fanout";
})(ZLinkLocationAutoConnectType || (exports.ZLinkLocationAutoConnectType = ZLinkLocationAutoConnectType = {}));
var ZLinkLocationRole;
(function (ZLinkLocationRole) {
    ZLinkLocationRole[ZLinkLocationRole["Invalid"] = 0] = "Invalid";
    // Value 1 is reserved for the removed gateway role. The numeric values are
    // serialized on the wire and in Redis row JSON, so they must not change.
    ZLinkLocationRole[ZLinkLocationRole["Spot"] = 2] = "Spot";
    ZLinkLocationRole[ZLinkLocationRole["Router"] = 3] = "Router";
    ZLinkLocationRole[ZLinkLocationRole["Dealer"] = 4] = "Dealer";
    ZLinkLocationRole[ZLinkLocationRole["Pub"] = 5] = "Pub";
    ZLinkLocationRole[ZLinkLocationRole["Sub"] = 6] = "Sub";
})(ZLinkLocationRole || (exports.ZLinkLocationRole = ZLinkLocationRole = {}));
var ZLinkRouteKind;
(function (ZLinkRouteKind) {
    ZLinkRouteKind[ZLinkRouteKind["Invalid"] = 0] = "Invalid";
    ZLinkRouteKind[ZLinkRouteKind["ActorSession"] = 1] = "ActorSession";
    ZLinkRouteKind[ZLinkRouteKind["SpotName"] = 2] = "SpotName";
    ZLinkRouteKind[ZLinkRouteKind["FrameworkRoute"] = 3] = "FrameworkRoute";
})(ZLinkRouteKind || (exports.ZLinkRouteKind = ZLinkRouteKind = {}));
var ZLinkLocationKind;
(function (ZLinkLocationKind) {
    ZLinkLocationKind[ZLinkLocationKind["Invalid"] = 0] = "Invalid";
    ZLinkLocationKind[ZLinkLocationKind["Peer"] = 1] = "Peer";
    ZLinkLocationKind[ZLinkLocationKind["Spot"] = 2] = "Spot";
    ZLinkLocationKind[ZLinkLocationKind["Actor"] = 3] = "Actor";
    ZLinkLocationKind[ZLinkLocationKind["Route"] = 4] = "Route";
    ZLinkLocationKind[ZLinkLocationKind["ClientServer"] = 5] = "ClientServer";
})(ZLinkLocationKind || (exports.ZLinkLocationKind = ZLinkLocationKind = {}));
