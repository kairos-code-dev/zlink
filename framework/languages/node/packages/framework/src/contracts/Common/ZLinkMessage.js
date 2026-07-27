"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkMessage = void 0;
exports.createZLinkMessageFromEncoded = createZLinkMessageFromEncoded;
exports.isZLinkMessage = isZLinkMessage;
const ZLinkEncodedPayload_1 = require("./ZLinkEncodedPayload");
const runtimeDecoders = new WeakMap();
class ZLinkMessage {
    value;
    encoded;
    constructor(value, encoded) {
        this.value = value;
        this.encoded = encoded;
    }
    static from(value) {
        return new ZLinkMessage(value, undefined);
    }
    static fromEncoded(payload) {
        return new ZLinkMessage(undefined, payload);
    }
    decode(type) {
        if (this.encoded === undefined) {
            return this.value;
        }
        const encoded = this.encoded.data();
        if (encoded.length === 0) {
            return undefined;
        }
        const decoder = runtimeDecoders.get(this);
        if (decoder !== undefined) {
            return decoder(type);
        }
        const text = Buffer.from(encoded).toString('utf8');
        try {
            return JSON.parse(text);
        }
        catch {
            return text;
        }
    }
    toEncodedPayload() {
        if (this.encoded !== undefined) {
            return this.encoded;
        }
        if (Buffer.isBuffer(this.value) || this.value instanceof Uint8Array) {
            return ZLinkEncodedPayload_1.ZLinkEncodedPayload.from(this.value);
        }
        return ZLinkEncodedPayload_1.ZLinkEncodedPayload.from(Buffer.from(JSON.stringify(this.value ?? null)));
    }
    isEncoded() {
        return this.encoded !== undefined;
    }
}
exports.ZLinkMessage = ZLinkMessage;
/** Internal runtime factory that preserves lazy typed decoding without exposing serializer selection. */
function createZLinkMessageFromEncoded(payload, decoder) {
    const message = ZLinkMessage.fromEncoded(payload);
    runtimeDecoders.set(message, decoder);
    return message;
}
function isZLinkMessage(value) {
    return value instanceof ZLinkMessage;
}
