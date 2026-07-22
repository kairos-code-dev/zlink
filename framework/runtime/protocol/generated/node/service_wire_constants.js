"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ServiceWireFlag = exports.ServiceWireCommand = exports.SERVICE_WIRE_REQUIRED_CAPABILITY = exports.SERVICE_WIRE_MAJOR = exports.SERVICE_WIRE_MAGIC = void 0;
// Generated from service-wire-v1.schema.json. Do not edit.
exports.SERVICE_WIRE_MAGIC = [90, 77];
exports.SERVICE_WIRE_MAJOR = 1;
exports.SERVICE_WIRE_REQUIRED_CAPABILITY = "framework-service-v11";
exports.ServiceWireCommand = {
    hello: 1,
    admit: 2,
    reject: 3,
    update: 4,
    livenessProbe: 5,
    livenessAck: 6,
    nodeSend: 16,
    nodeRequest: 17,
    channelSend: 18,
    channelRequest: 19,
    reply: 20,
    spotSend: 21,
    spotRequest: 22,
    logicalMulticast: 23,
    actorSend: 24,
    actorRequest: 25,
    actorLookup: 26,
    actorDestroy: 27,
    actorJoin: 28,
    actorLeft: 29,
    transferReady: 30,
    transferData: 31,
    transferAck: 32,
    replyRelay: 33,
    transferSeal: 34,
    transferComplete: 35,
    boundSessionSend: 36,
    actorJoined: 37,
    boundSessionBind: 38,
    instanceSpot: 39,
    transferPrepare: 40,
    transferReserved: 41,
    sessionTransferSeal: 42,
    sessionTransferSealed: 43,
    sessionTransferRoute: 44,
    sessionTransferRouted: 45,
    replyRelayAck: 46,
};
exports.ServiceWireFlag = {
    metadata: 1,
    boundSession: 2,
    sourceSpotRid: 4,
    extension: 8,
};
