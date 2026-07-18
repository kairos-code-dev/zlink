// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
// The ready-domain mask that drains both the application and infrastructure
// domains in one pass (ZLINK_MESH_READY_APPLICATION | _INFRASTRUCTURE). The
// binding surfaces the mask as a plain number, so samples spell it out here.
const READY_ALL = 3;
// A received record's `kind` and `operationKind` are the raw core enum values
// (zlink_mesh_record_kind_t / zlink_mesh_operation_kind_t). The samples spell
// out the ones they need rather than depend on the higher-level contract enums,
// which describe a different, coarser taxonomy.
const MeshRecordKind = Object.freeze({
    NodeSend: 1, NodeRequest: 2, ChannelSend: 3, ChannelRequest: 4,
    SpotSend: 5, SpotRequest: 6, SpotMulticast: 7, SpotControl: 8,
    ActorSend: 9, ActorRequest: 10, Completion: 11, SendReady: 12, TransferControl: 13
});
const MeshOperationKind = Object.freeze({
    NodeRequest: 1, ChannelRequest: 2, SpotRequest: 3, ActorRequest: 4,
    ActorLookup: 5, ActorDestroy: 6, ActorJoin: 7, ActorLeave: 8,
    StreamBind: 9, StreamUnbind: 10, StreamClose: 11
});
// A request record carries a reply token, so it is the shape a responder replies
// to (as opposed to sends, completions, or controls).
function isRequestRecord(record) {
    return record.kind === MeshRecordKind.NodeRequest
        || record.kind === MeshRecordKind.ChannelRequest
        || record.kind === MeshRecordKind.SpotRequest
        || record.kind === MeshRecordKind.ActorRequest;
}
// An actor-join arrives as a SPOT_CONTROL record whose operation kind is
// ACTOR_JOIN; it is answered with replyActorJoin().
function isActorJoinRequest(record) {
    return record.kind === MeshRecordKind.SpotControl
        && record.operationKind === MeshOperationKind.ActorJoin;
}
// Build a record handler that appends the string payloads of every actor-send
// record into `sink`, ignoring completions, controls, and other record kinds.
function collectActorPayloads(sink) {
    return (record) => {
        if (record != null && record.kind === MeshRecordKind.ActorSend) {
            for (const part of record.parts)
                sink.push(part.data().toString());
        }
    };
}
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function tcpEndpoint() {
    return `tcp://127.0.0.1:${await reservePort()}`;
}
async function waitUntil(predicate, timeoutMs, message) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (predicate())
            return;
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error(message);
}
// A RouteMesh peer is usable once the far node has been admitted into the mesh.
async function waitPeerAdmitted(node, timeoutMs = 5000) {
    await waitUntil(() => node.status().admittedPeerCount > 0, timeoutMs, 'mesh peer was not admitted');
}
function frame(payload) {
    const framed = Buffer.allocUnsafe(payload.length + 6);
    framed.writeUInt16BE(0, 0);
    framed.writeUInt32BE(payload.length, 2);
    payload.copy(framed, 6);
    return framed;
}
function operationIdEquals(a, b) {
    return a != null && b != null && a.high === b.high && a.low === b.low;
}
// Pull-based dispatch driver for one MeshNode.
//
// RouteMesh 10.0.0 replaces push callbacks with a ready index: the node marks
// owners (node/spot/actor) readable, the consumer drains those owners into a
// reusable ready batch, claims each ready record, and materializes its messages
// into a reusable receive batch. `MeshPump` wraps that loop so a sample can just
// pass a per-record callback and forget the batch bookkeeping.
class MeshPump {
    node;
    ready;
    receive;
    constructor(node, sizes = {}) {
        this.node = node;
        this.ready = node.createReadyBatch(sizes.readyCapacity ?? 16);
        this.receive = node.createReceiveBatch(sizes.messageCapacity ?? 32, sizes.partCapacity ?? 128, sizes.byteCapacity ?? (1 << 20));
    }
    // Drain everything currently ready, invoking `onRecord` for each materialized
    // receive record. Non-blocking: returns as soon as the ready index is empty.
    drain(onRecord) {
        for (;;) {
            // A batch must be empty (new or reset) before each drain pass.
            this.ready.reset();
            const drained = this.node.drainReady(READY_ALL, this.ready, zlink.RecvFlags.DontWait);
            if (!drained.ok || drained.records.length === 0)
                return;
            for (let i = 0; i < drained.records.length; i += 1) {
                const claim = this.ready.takeClaim(i);
                try {
                    this.receive.reset();
                    const received = claim.recvBatch(this.receive, zlink.RecvFlags.DontWait);
                    if (received.ok) {
                        for (const record of received.records)
                            onRecord(record);
                    }
                }
                finally {
                    claim.release();
                }
            }
            if (!drained.hasResidue)
                return;
        }
    }
    // Drain repeatedly until `predicate` holds or the deadline passes.
    async pumpUntil(predicate, timeoutMs, onRecord, message = 'mesh dispatch timed out') {
        const deadline = Date.now() + timeoutMs;
        for (;;) {
            this.drain(onRecord);
            if (predicate())
                return;
            if (Date.now() >= deadline)
                throw new Error(message);
            await new Promise((resolve) => setTimeout(resolve, 5));
        }
    }
    // Drive dispatch until the completion for `operationId` is observed, returning
    // the completion record (it carries terminalResult and any reply parts). Any
    // other record drained along the way is forwarded to `onRecord`.
    async awaitCompletion(operationId, timeoutMs, onRecord = () => { }) {
        let completion = null;
        await this.pumpUntil(() => completion !== null, timeoutMs, (record) => {
            if (record.kind === MeshRecordKind.Completion
                && operationIdEquals(record.operationId, operationId)) {
                completion = record;
            }
            else {
                onRecord(record);
            }
        }, 'mesh operation did not complete');
        return completion;
    }
    close() {
        this.receive.close();
        this.ready.close();
    }
}
// Join `actor` into `spot` on its own node and accept the join. On a single node
// the SPOT_CONTROL join request and its COMPLETION both surface on one pump, so
// the accept happens inside awaitCompletion. Returns the join completion record.
async function joinActorToSpot(pump, node, actor, spot, payload, options = {}) {
    const timeoutMs = options.timeoutMs ?? 2000;
    const nodeRid = node.status().routingId;
    const spotRid = spot.routingId;
    const spotGeneration = spot.status().lifecycleGeneration;
    const operationId = node.joinActorSpot(actor, nodeRid, spotRid, spotGeneration, Buffer.from(payload), timeoutMs);
    return pump.awaitCompletion(operationId, timeoutMs, (record) => {
        if (isActorJoinRequest(record)) {
            record.replyActorJoin(0, Buffer.from('accepted'));
        }
        else if (options.onMessage) {
            options.onMessage(record);
        }
    });
}
// Leave `actor` from whatever spot it currently occupies.
async function leaveActorFromSpot(pump, node, actor, options = {}) {
    const timeoutMs = options.timeoutMs ?? 2000;
    const location = node.actorLookup(actor.actorId);
    const operationId = node.leaveActor(actor, location.membershipEpoch, timeoutMs);
    return pump.awaitCompletion(operationId, timeoutMs, options.onMessage);
}
// Destroy `actor`, draining until the destroy completion arrives.
async function destroyMeshActor(pump, node, actor, options = {}) {
    const timeoutMs = options.timeoutMs ?? 2000;
    const operationId = node.destroyActor(actor, timeoutMs);
    return pump.awaitCompletion(operationId, timeoutMs, options.onMessage);
}
module.exports = {
    MeshPump,
    MeshOperationKind,
    MeshRecordKind,
    READY_ALL,
    collectActorPayloads,
    destroyMeshActor,
    frame,
    isActorJoinRequest,
    isRequestRecord,
    joinActorToSpot,
    leaveActorFromSpot,
    operationIdEquals,
    reservePort,
    tcpEndpoint,
    waitPeerAdmitted,
    waitUntil
};
