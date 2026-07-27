"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkTimerOverrunPolicy = void 0;
var ZLinkTimerOverrunPolicy;
(function (ZLinkTimerOverrunPolicy) {
    ZLinkTimerOverrunPolicy["SkipLateTicks"] = "skipLateTicks";
    ZLinkTimerOverrunPolicy["CatchUpBounded"] = "catchUpBounded";
    ZLinkTimerOverrunPolicy["DelayNextTick"] = "delayNextTick";
})(ZLinkTimerOverrunPolicy || (exports.ZLinkTimerOverrunPolicy = ZLinkTimerOverrunPolicy = {}));
