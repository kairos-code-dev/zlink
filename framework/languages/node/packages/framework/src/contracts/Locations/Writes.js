"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkLocationWriteStatus = exports.ZLinkLocationWriteIntent = void 0;
var ZLinkLocationWriteIntent;
(function (ZLinkLocationWriteIntent) {
    ZLinkLocationWriteIntent[ZLinkLocationWriteIntent["NewClaim"] = 1] = "NewClaim";
    ZLinkLocationWriteIntent[ZLinkLocationWriteIntent["Renew"] = 2] = "Renew";
    ZLinkLocationWriteIntent[ZLinkLocationWriteIntent["Takeover"] = 3] = "Takeover";
})(ZLinkLocationWriteIntent || (exports.ZLinkLocationWriteIntent = ZLinkLocationWriteIntent = {}));
var ZLinkLocationWriteStatus;
(function (ZLinkLocationWriteStatus) {
    ZLinkLocationWriteStatus["Stored"] = "stored";
    ZLinkLocationWriteStatus["IgnoredStale"] = "ignoredStale";
    ZLinkLocationWriteStatus["RejectedConflict"] = "rejectedConflict";
})(ZLinkLocationWriteStatus || (exports.ZLinkLocationWriteStatus = ZLinkLocationWriteStatus = {}));
