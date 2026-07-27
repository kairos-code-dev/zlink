"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkAuthorityScanCursor = void 0;
class ZLinkAuthorityScanCursor {
    encoded;
    constructor(encoded) {
        this.encoded = encoded;
    }
    static from(encoded) {
        const byteLength = Buffer.byteLength(encoded, 'utf8');
        if (byteLength < 1 || byteLength > 4096) {
            throw new RangeError('Authority scan cursor must contain 1..4096 UTF-8 bytes.');
        }
        return new ZLinkAuthorityScanCursor(encoded);
    }
}
exports.ZLinkAuthorityScanCursor = ZLinkAuthorityScanCursor;
