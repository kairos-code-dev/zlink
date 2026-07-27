"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLinkMessageMetadataEmpty = void 0;
exports.zlinkMessageMetadata = zlinkMessageMetadata;
class ImmutableZLinkMessageMetadata {
    values;
    constructor(values = new Map()) {
        this.values = Object.freeze(new ImmutableMetadataMap(values));
    }
    find(key) {
        return this.values.get(key);
    }
}
class ImmutableMetadataMap {
    #values;
    constructor(values) {
        this.#values = new Map(typeof values[Symbol.iterator] === 'function'
            ? values
            : Object.entries(values));
    }
    get size() {
        return this.#values.size;
    }
    get(key) {
        return this.#values.get(key);
    }
    has(key) {
        return this.#values.has(key);
    }
    forEach(callbackfn, thisArg) {
        this.#values.forEach((value, key) => callbackfn.call(thisArg, value, key, this));
    }
    entries() {
        return this.#values.entries();
    }
    keys() {
        return this.#values.keys();
    }
    values() {
        return this.#values.values();
    }
    [Symbol.iterator]() {
        return this.#values[Symbol.iterator]();
    }
}
exports.ZLinkMessageMetadataEmpty = Object.freeze(new ImmutableZLinkMessageMetadata());
function zlinkMessageMetadata(values) {
    return Object.freeze(new ImmutableZLinkMessageMetadata(values));
}
