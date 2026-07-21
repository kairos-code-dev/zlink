'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');
test('MeshNode without channel membership uses node, peer, and shutdown surfaces', () => {
    const context = zlink.createContext();
    const suffix = `${process.pid}-${Date.now()}`;
    const meshName = `node-binding-zero-membership-${suffix}`;
    const node = zlink.createMeshNode(context, { meshName });
    try {
        node.setRoutingId(zlink.RoutingId.from(`zero-node-${suffix}`));
        node.setBind(`inproc://${meshName}`);
        // No addChannelName call: a caller-only MeshNode is valid.
        node.start();
        assert.equal(node.status().state, zlink.MeshNodeState.Ready);
        assert.equal(node.status().channelCount, 0);
        const intent = node.connectPeer({ endpoint: `inproc://${meshName}-missing` });
        assert.notEqual(intent, 0n);
        const [peer] = node.peers();
        assert.ok(peer);
        assert.equal(peer.connectionIntentId, intent);
        assert.equal(peer.channelCount, 0);
        const payload = zlink.Message.from('zero-direct');
        try {
            assert.equal(node.sendToNode(zlink.RoutingId.from('missing-node'), payload), zlink.SubmitResult.NotConnected);
        }
        finally {
            payload.close();
        }
        assert.equal(node.shutdown(1000), zlink.RequestResult.Ok);
        assert.equal(node.status().state, zlink.MeshNodeState.Stopped);
    }
    finally {
        node.close();
        context.close();
    }
});
test('MeshNode routing id can be configured before start', () => {
    const context = zlink.createContext();
    const meshName = `node-binding-routing-${process.pid}`;
    const node = zlink.createMeshNode(context, { meshName });
    try {
        node.setRoutingId(zlink.RoutingId.from(`node-${process.pid}`));
        node.setBind(`inproc://${meshName}`);
        node.addChannelName('binding.contract');
        node.start();
        const status = node.status();
        assert.equal(status.meshName, meshName);
        assert.equal(status.routingId.toString(), `node-${process.pid}`);
        assert.equal(status.channelCount, 1);
        assert.equal(node.shutdown(1000), zlink.RequestResult.Ok);
    }
    finally {
        node.close();
        context.close();
    }
});
test('MeshNode connecting peer exposes no routing id until admission', () => {
    const context = zlink.createContext();
    const meshName = `node-binding-connecting-peer-${process.pid}`;
    const node = zlink.createMeshNode(context, { meshName });
    try {
        node.setRoutingId(zlink.RoutingId.from(`node-${process.pid}`));
        node.setBind(`inproc://${meshName}`);
        node.addChannelName('binding.contract');
        node.start();
        node.connectPeer({ endpoint: `inproc://${meshName}-missing` });
        const peer = node.peers().find((entry) => entry.endpoint === `inproc://${meshName}-missing`);
        assert.ok(peer);
        assert.equal(peer.state, 2);
        assert.equal(peer.routingId, null);
        assert.equal(node.shutdown(1000), zlink.RequestResult.Ok);
    }
    finally {
        node.close();
        context.close();
    }
});
test('MeshNode bound-session send requires an explicit nonzero binding generation', () => {
    const context = zlink.createContext();
    const suffix = `${process.pid}-${Date.now()}`;
    const meshName = `node-binding-session-generation-${suffix}`;
    const node = zlink.createMeshNode(context, { meshName });
    try {
        node.setRoutingId(zlink.RoutingId.from(`node-${suffix}`));
        node.setBind(`inproc://${meshName}`);
        node.start();
        const actor = node.createActor('session-generation-actor');
        assert.equal(node.sendActorBoundSession(actor, 0n, Buffer.from('must-not-route')), zlink.SubmitResult.InvalidArgument);
        assert.equal(node.shutdown(1000), zlink.RequestResult.Ok);
    }
    finally {
        node.close();
        context.close();
    }
});
