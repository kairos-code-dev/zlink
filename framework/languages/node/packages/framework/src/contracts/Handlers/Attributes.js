"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZLINK_DECORATOR_METADATA = void 0;
exports.ZLinkHandlerGroup = ZLinkHandlerGroup;
exports.ZLinkRequest = ZLinkRequest;
exports.ZLinkSend = ZLinkSend;
exports.ZLinkPublish = ZLinkPublish;
exports.ZLinkPacket = ZLinkPacket;
exports.ZLinkSpotRequest = ZLinkSpotRequest;
exports.ZLinkSpotSubscription = ZLinkSpotSubscription;
exports.ZLinkSpotActorSend = ZLinkSpotActorSend;
exports.ZLinkSpotActorRequest = ZLinkSpotActorRequest;
exports.ZLinkStreamPacket = ZLinkStreamPacket;
exports.ZLinkStreamRaw = ZLinkStreamRaw;
exports.readZLinkDecoratorMetadata = readZLinkDecoratorMetadata;
exports.ZLINK_DECORATOR_METADATA = Symbol.for('@zlink-systems/framework:decorator');
function ZLinkHandlerGroup(groupName) {
    return classDecorator({ kind: 'handlerGroup', groupName });
}
function ZLinkRequest(packetName) {
    return methodDecorator({ kind: 'request', packetName });
}
function ZLinkSend(packetName) {
    return methodDecorator({ kind: 'send', packetName });
}
function ZLinkPublish(packetName) {
    return methodDecorator({ kind: 'publish', packetName });
}
function ZLinkPacket(packetName) {
    return classDecorator({ kind: 'packet', packetName });
}
function ZLinkSpotRequest(packetName) {
    return methodDecorator({ kind: 'spotRequest', packetName });
}
function ZLinkSpotSubscription(channelName, topic) {
    return methodDecorator({ kind: 'spotSubscription', channelName, topic });
}
function ZLinkSpotActorSend(packetName) {
    return methodDecorator({ kind: 'spotActorSend', packetName });
}
function ZLinkSpotActorRequest(packetName) {
    return methodDecorator({ kind: 'spotActorRequest', packetName });
}
function ZLinkStreamPacket() {
    return methodDecorator({ kind: 'streamPacket' });
}
function ZLinkStreamRaw() {
    return methodDecorator({ kind: 'streamRaw' });
}
function classDecorator(metadata) {
    return (target) => appendMetadata(target, metadata);
}
function methodDecorator(metadata) {
    return (target, propertyKey) => appendMetadata(target.constructor, {
        ...metadata,
        methodName: String(propertyKey)
    });
}
function appendMetadata(target, metadata) {
    const current = readZLinkDecoratorMetadata(target);
    Object.defineProperty(target, exports.ZLINK_DECORATOR_METADATA, {
        configurable: true,
        enumerable: false,
        value: [...current, metadata],
        writable: false
    });
}
function readZLinkDecoratorMetadata(target) {
    if (!Object.prototype.hasOwnProperty.call(target, exports.ZLINK_DECORATOR_METADATA)) {
        return [];
    }
    return target[exports.ZLINK_DECORATOR_METADATA];
}
