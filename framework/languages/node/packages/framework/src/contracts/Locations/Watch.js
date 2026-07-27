"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkLocationChangeScopeKind = exports.ZLinkLocationChangeType = void 0;
var ZLinkLocationChangeType;
(function (ZLinkLocationChangeType) {
    ZLinkLocationChangeType["Upserted"] = "upserted";
    ZLinkLocationChangeType["Removed"] = "removed";
    ZLinkLocationChangeType["Expired"] = "expired";
})(ZLinkLocationChangeType || (exports.ZLinkLocationChangeType = ZLinkLocationChangeType = {}));
var ZLinkLocationChangeScopeKind;
(function (ZLinkLocationChangeScopeKind) {
    ZLinkLocationChangeScopeKind["MeshNode"] = "meshNode";
    ZLinkLocationChangeScopeKind["ClientServer"] = "clientServer";
    ZLinkLocationChangeScopeKind["Spot"] = "spot";
    ZLinkLocationChangeScopeKind["Authority"] = "authority";
    ZLinkLocationChangeScopeKind["OwnerLease"] = "ownerLease";
    ZLinkLocationChangeScopeKind["FanoutPublisher"] = "fanoutPublisher";
})(ZLinkLocationChangeScopeKind || (exports.ZLinkLocationChangeScopeKind = ZLinkLocationChangeScopeKind = {}));
