"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkEncodedPayload = void 0;
class ZLinkEncodedPayload {
    payload;
    constructor(bytes) {
        this.payload = Buffer.from(bytes);
    }
    static from(bytes) {
        return new ZLinkEncodedPayload(bytes);
    }
    data() {
        return new Uint8Array(this.payload);
    }
    toBytes() {
        return this.data();
    }
    copy() {
        return ZLinkEncodedPayload.from(this.payload);
    }
    size() {
        return this.payload.length;
    }
    isEmpty() {
        return this.payload.length === 0;
    }
    getString(encoding = 'utf8') {
        return this.payload.toString(encoding);
    }
    close() {
        // ZLinkEncodedPayload owns managed memory only; this mirrors Message-like readers.
    }
}
exports.ZLinkEncodedPayload = ZLinkEncodedPayload;
