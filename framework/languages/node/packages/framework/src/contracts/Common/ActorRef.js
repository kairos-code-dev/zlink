"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.zlinkActorRefSnapshotFrom = zlinkActorRefSnapshotFrom;
exports.zlinkActorRefSnapshotToActorRef = zlinkActorRefSnapshotToActorRef;
function zlinkActorRefSnapshotFrom(actorRef) {
    return {
        nodeRid: String(actorRef.nodeRid),
        actorId: actorRef.actorId,
        generation: actorRef.generation
    };
}
function zlinkActorRefSnapshotToActorRef(snapshot) {
    return {
        nodeRid: String(snapshot.nodeRid),
        actorId: snapshot.actorId,
        // JSON transports carry uint64 values as decimal strings. Normalize the
        // wire value at the public conversion boundary so callers keep bigint.
        generation: BigInt(snapshot.generation)
    };
}
