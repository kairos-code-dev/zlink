"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.zlinkDefaultLocationOptions = exports.zlinkRuntimeDefaultLocationOptions = void 0;
exports.zlinkRuntimeDefaultLocationOptions = {
    heartbeatIntervalMs: 10000,
    ownerLeaseTtlMs: 30000,
    pollingIntervalMs: 1000,
    listPageSize: 1000,
    storeFailureGraceMs: 30000,
    routingIdFencingMarginMs: 5000,
    ownerLeaseRenewTimeoutMs: 3000,
    routeCacheMaxAgeMs: 15000,
    relocationForwardingWindowMs: 30000,
    maxActiveOutboundRelocations: 64,
    maxActiveInboundRelocations: 64,
    maxConcurrentRelocationCaptures: 8,
    maxConcurrentRelocationRestores: 8,
    maxRelocationPayloadInFlightBytes: 268435456
};
exports.zlinkDefaultLocationOptions = exports.zlinkRuntimeDefaultLocationOptions;
