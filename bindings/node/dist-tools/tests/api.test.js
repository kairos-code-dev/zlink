'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
const spotActorModels = require('../dist/zlink/runtime/service/spot/actor_models');
const SERVICE_ROLE_SPOT = 2;
const SERVICE_KIND_SPOT_PUB = 4;
test('package exports only expose the public contract entry point', () => {
    assert.throws(() => require('@zlink-systems/zlink/zlink/runtime/native/native'), { code: 'ERR_PACKAGE_PATH_NOT_EXPORTED' });
    assert.throws(() => require('@zlink-systems/zlink/dist/zlink/runtime/native/native'), { code: 'ERR_PACKAGE_PATH_NOT_EXPORTED' });
});
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
test('service objects expose aligned monitor and query surface', () => {
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    ctx.recalculateAutoHwm();
    assert.equal(zlink.createRegistry, undefined);
    assert.equal(zlink.createDiscovery, undefined);
    assert.equal(zlink.createRegistryQueryClient, undefined);
    assert.equal(typeof spot.setDispatchHandler, 'function');
    assert.equal(spot.drainChannelReplyFrom, undefined);
    assert.equal(node.peers().length, 0);
    assert.equal(node.subjects().length, 0);
    node.setRoutingId(zlink.RoutingId.from(Buffer.from('node-id')));
    assert.equal(node.routingId.toBytes().toString(), 'node-id');
    spot.setRoutingId(zlink.RoutingId.from(Buffer.from('spot-id')));
    assert.equal(spot.routingId.toBytes().toString(), 'spot-id');
    const entrySpot = node.entrySpot();
    entrySpot.setRoutingId(zlink.RoutingId.from(Buffer.from('entry-spot-id')));
    const lookedUpSpot = node.spotLookup(zlink.RoutingId.from(Buffer.from('entry-spot-id')));
    assert.ok(lookedUpSpot);
    assert.equal(lookedUpSpot.routingId.toBytes().toString(), 'entry-spot-id');
    lookedUpSpot.close();
    entrySpot.close();
    const roomRid = zlink.RoutingId.from(Buffer.from('node-room-id'));
    const firstRoom = node.getOrCreateSpot(roomRid);
    const secondRoom = node.getOrCreateSpot(roomRid);
    assert.equal(firstRoom.created, true);
    assert.equal(secondRoom.created, false);
    assert.equal(firstRoom.spot.routingId.toBytes().toString(), 'node-room-id');
    assert.equal(secondRoom.spot.routingId.toBytes().toString(), 'node-room-id');
    firstRoom.spot.close();
    secondRoom.spot.close();
    assert.equal(zlink.SpotNodeOption, undefined);
    assert.equal(zlink.SpotNodePubMode, undefined);
    assert.equal(zlink.SpotNodePubQueueFullPolicy, undefined);
    assert.equal(zlink.MonitorSocket, undefined);
    assert.equal(zlink.SpotNode, undefined);
    assert.equal(zlink.Registry, undefined);
    assert.equal(zlink.Discovery, undefined);
    assert.equal(zlink.RegistryQueryClient, undefined);
    assert.equal(node.setDiscovery, undefined);
    assert.equal(node.attachDiscovery, undefined);
    assert.equal(node.register, undefined);
    assert.equal(node.unregister, undefined);
    assert.equal(node.pubSocket, undefined);
    assert.equal(node.subSocket, undefined);
    assert.equal(node.pubPeers, undefined);
    assert.equal(node.subPeers, undefined);
    spot.close();
    node.close();
    ctx.close();
});
test('actor join info without source Spot RID round-trips for native join replies', () => {
    const sourceNodeRid = zlink.RoutingId.from('source-node');
    const targetNodeRid = zlink.RoutingId.from('target-node');
    const targetSpotRid = zlink.RoutingId.from('target-spot');
    const raw = {
        sourceActor: {
            nodeRid: sourceNodeRid.toBytes(),
            actorId: 'player-1',
            generation: 1n
        },
        targetActor: {
            nodeRid: targetNodeRid.toBytes(),
            actorId: 'player-1',
            generation: 2n
        },
        sourceNodeRid: sourceNodeRid.toBytes(),
        sourceSpotRid: null,
        targetNodeRid: targetNodeRid.toBytes(),
        targetSpotRid: targetSpotRid.toBytes(),
        joinEpoch: 3n,
        flags: 1,
        requestHandle: 42n
    };
    const info = spotActorModels.actorJoinInfoFromRaw(raw);
    assert.equal(info.sourceSpotRid, undefined);
    const replyRaw = spotActorModels.actorJoinInfoToRaw(info);
    assert.equal(replyRaw.sourceSpotRid, null);
    assert.equal(replyRaw.requestHandle, 42n);
});
test('Spot must be created through SpotNode.createSpot()', () => {
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    assert.equal(zlink.Spot, undefined);
    node.close();
    ctx.close();
});
test('utility wrappers follow the canonical surface', () => {
    const counter = zlink.createAtomicCounter(3);
    assert.equal(counter.value(), 3);
    assert.equal(counter.inc(), 3);
    assert.equal(counter.value(), 4);
    assert.equal(counter.dec(), 1);
    assert.equal(counter.value(), 3);
    counter.set(0);
    counter.dec();
    assert.equal(counter.value(), -1);
    counter.close();
    const thread = zlink.createThread(() => { });
    thread.join();
});
test('rid disconnect surface exists on sockets and spot nodes', () => {
    const ctx = zlink.createContext();
    const pair = zlink.createPairSocket(ctx);
    const dealer = zlink.createDealerSocket(ctx);
    const stream = zlink.createStreamSocket(ctx);
    const node = zlink.createSpotNode(ctx);
    assert.equal(typeof pair.disconnectRid, 'function');
    assert.equal(typeof dealer.disconnectRid, 'function');
    assert.equal(typeof stream.disconnectRid, 'function');
    assert.equal(typeof node.connectPeerRid, 'function');
    assert.equal(typeof node.disconnectPeerRid, 'function');
    node.close();
    stream.close();
    dealer.close();
    pair.close();
    ctx.close();
});
test('removed receiver stays removed from aligned api', () => {
    assert.equal('Receiver' in zlink, false);
    assert.equal(zlink.Receiver, undefined);
});
test('context options, shutdown, and tls facades follow the aligned surface', () => {
    const ctx = zlink.createContext();
    const pair = zlink.createPairSocket(ctx);
    const dealer = zlink.createDealerSocket(ctx);
    const node = zlink.createSpotNode(ctx);
    assert.equal(typeof ctx.shutdown, 'function');
    assert.equal(typeof ctx.options, 'object');
    assert.equal(typeof ctx.options.ioThreads, 'number');
    assert.equal(typeof ctx.options.maxSockets, 'number');
    assert.equal(typeof ctx.options.socketLimit, 'number');
    assert.equal(typeof ctx.options.maxMsgSize, 'number');
    assert.equal(typeof ctx.options.msgTSize, 'number');
    assert.equal(typeof ctx.options.threadPriority, 'number');
    assert.equal(typeof ctx.options.blocky, 'boolean');
    assert.equal(typeof ctx.options.autoHwmProfile, 'number');
    assert.equal(ctx.options.autoHwmMsgUnitBytes, 0);
    ctx.options.ioThreads = ctx.options.ioThreads;
    ctx.options.maxSockets = ctx.options.maxSockets;
    ctx.options.maxMsgSize = ctx.options.maxMsgSize;
    ctx.options.blocky = ctx.options.blocky;
    ctx.options.autoHwmProfile = zlink.AutoHwmProfile.Compact;
    assert.equal(ctx.options.autoHwmProfile, zlink.AutoHwmProfile.Compact);
    ctx.options.autoHwmMsgUnitBytes = 64;
    assert.equal(ctx.options.autoHwmMsgUnitBytes, 64);
    ctx.options.autoHwmMsgUnitBytes = 0;
    assert.equal(ctx.options.autoHwmMsgUnitBytes, 0);
    assert.throws(() => {
        ctx.options.autoHwmMsgUnitBytes = -1;
    }, RangeError);
    assert.equal('autoHwmMsgUnitBytes' in pair.options, false);
    try {
        assert.equal(typeof ctx.options.threadSchedulingPolicy, 'number');
        ctx.options.threadSchedulingPolicy = ctx.options.threadSchedulingPolicy;
    }
    catch (error) {
        assert.ok(error instanceof zlink.ConfigError);
    }
    assert.equal(typeof pair.setTlsServer, 'function');
    assert.equal(typeof pair.setTlsClient, 'function');
    assert.equal(typeof pair.setChannelName, 'undefined');
    assert.equal(typeof pair.getChannelName, 'undefined');
    assert.equal(typeof dealer.setChannelName, 'function');
    assert.equal(typeof dealer.getChannelName, 'function');
    assert.equal(typeof node.setTlsServer, 'function');
    assert.equal(typeof node.setTlsClient, 'function');
    assert.throws(() => pair.setTlsServer(Buffer.from('cert'), 'key'), /cert/);
    assert.throws(() => pair.setTlsClient(Buffer.from('ca'), 'host'), /ca/);
    assert.throws(() => node.setTlsServer(Buffer.from('cert'), 'key'), /cert/);
    assert.throws(() => node.setTlsClient(Buffer.from('ca'), 'host'), /ca/);
    const message = zlink.Message.from('message');
    const messageCopy = message.copy();
    const destination = Buffer.alloc(7);
    assert.equal(typeof message.close, 'function');
    assert.equal(message.size(), 7);
    assert.equal(message.isEmpty(), false);
    assert.equal(message.getString(), 'message');
    assert.equal(message.toString(), 'message');
    assert.equal(message.copyTo(destination), 7);
    assert.equal(destination.toString(), 'message');
    assert.equal(messageCopy.toBytes().toString(), 'message');
    assert.equal(message.value, undefined);
    assert.equal(message.packetName, undefined);
    assert.equal(message.withPacketName, undefined);
    const allocatedMessage = zlink.Message.allocate(3);
    allocatedMessage.data()[0] = 0x01;
    allocatedMessage.data()[1] = 0x02;
    allocatedMessage.data()[2] = 0x03;
    assert.deepEqual([...allocatedMessage.data()], [0x01, 0x02, 0x03]);
    assert.equal(zlink.Message.allocate(0).size(), 0);
    assert.equal(zlink.Message.allocate(0).isEmpty(), true);
    node.close();
    dealer.close();
    pair.close();
    ctx.shutdown();
    ctx.close();
});
test('runtime-only bridge methods are not exposed on public objects', () => {
    const ctx = zlink.createContext();
    const pair = zlink.createPairSocket(ctx);
    assert.equal(ctx.nativeHandle, undefined);
    assert.equal(ctx.getOptionRawInternal, undefined);
    assert.equal(ctx.getOptionRawStrictInternal, undefined);
    assert.equal(ctx.setOptionRawInternal, undefined);
    assert.equal(pair.nativeHandle, undefined);
    pair.close();
    ctx.close();
});
test('native failures surface documented zlink error classes', () => {
    const ctx = zlink.createContext();
    const pair = zlink.createPairSocket(ctx);
    const monitor = pair.monitorOpen();
    try {
        assert.throws(() => pair.bind('bad://endpoint'), zlink.BindError);
        assert.throws(() => pair.connect('bad://endpoint'), zlink.ConnectError);
        monitor.onEvent(() => { });
        assert.throws(() => monitor.recv(), zlink.RecvError);
        monitor.close();
        assert.throws(() => monitor.status(), zlink.ConfigError);
    }
    finally {
        pair.close();
        ctx.close();
    }
});
test('spot close leaves spot node alive', async () => {
    const ctx = zlink.createContext();
    const standaloneNode = zlink.createSpotNode(ctx);
    const spot = standaloneNode.createSpot();
    const standaloneEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    standaloneNode.setPubBind(standaloneEndpoint);
    assert.ok(standaloneNode.status().nodeRoutingId instanceof zlink.RoutingId);
    spot.close();
    assert.equal(standaloneNode.status().configuredPeerCount, 0);
    standaloneNode.close();
    ctx.close();
});
test('canonical socket options are exposed through typed facades', () => {
    const ctx = zlink.createContext();
    const pair = zlink.createPairSocket(ctx);
    const dealer = zlink.createDealerSocket(ctx);
    const router = zlink.createRouterSocket(ctx);
    const stream = zlink.createStreamSocket(ctx);
    const pub = zlink.createPubSocket(ctx);
    const xpub = zlink.createXPubSocket(ctx);
    const sub = zlink.createSubSocket(ctx);
    pair.options.linger = 0;
    pair.options.sendHwm = 7;
    pair.options.recvTimeout = 13;
    pair.options.immediate = true;
    pair.options.connectTimeout = 17;
    pair.options.ipv6 = false;
    pair.options.tcpNoDelay = true;
    pair.options.tcpKeepalive = 1;
    pair.options.heartbeatInterval = 100;
    pair.options.heartbeatTtl = 200;
    pair.options.heartbeatTimeout = 300;
    pair.options.maxMsgSize = 4096n;
    pair.options.backlog = 9;
    pair.options.reconnectInterval = 21;
    pair.options.reconnectIntervalMax = 34;
    pair.options.submitRetryMode = zlink.SubmitRetryMode.LocalFailure;
    pair.options.submitRetryTimeout = 42;
    pair.options.submitRetryAttempts = 2;
    dealer.options.probe = true;
    router.options.mandatory = true;
    pair.options.ridDuplicatePolicy = zlink.RidDuplicatePolicy.Handover;
    router.options.probe = true;
    router.options.setConnectRoutingId(zlink.RoutingId.from(Buffer.from('route')));
    stream.options.notify = true;
    pub.options.verbose = true;
    pub.options.verboser = true;
    pub.options.noDrop = true;
    pub.options.manual = true;
    pub.options.manualLastValue = true;
    pub.options.setWelcomeMessage('welcome');
    xpub.options.verbose = true;
    xpub.options.verboser = true;
    xpub.options.noDrop = true;
    xpub.options.manual = true;
    xpub.options.manualLastValue = true;
    xpub.options.setWelcomeMessage('welcome');
    sub.setSubscription('topic');
    assert.equal(pair.options.linger, 0);
    assert.equal(pair.options.sendHwm, 7);
    assert.equal(pair.options.recvTimeout, 13);
    assert.equal(pair.options.immediate, true);
    assert.equal(pair.options.connectTimeout, 17);
    assert.equal(pair.options.ipv6, false);
    assert.equal(pair.options.tcpNoDelay, true);
    assert.equal(pair.options.tcpKeepalive, 1);
    assert.equal(pair.options.heartbeatInterval, 100);
    assert.equal(pair.options.heartbeatTtl, 200);
    assert.equal(pair.options.heartbeatTimeout, 300);
    assert.equal(pair.options.maxMsgSize, 4096n);
    assert.equal(pair.options.backlog, 9);
    assert.equal(pair.options.reconnectInterval, 21);
    assert.equal(pair.options.reconnectIntervalMax, 34);
    assert.equal(pair.options.submitRetryMode, zlink.SubmitRetryMode.LocalFailure);
    assert.equal(pair.options.submitRetryTimeout, 42);
    assert.equal(pair.options.submitRetryAttempts, 2);
    assert.equal(pair.options.ridDuplicatePolicy, zlink.RidDuplicatePolicy.Handover);
    assert.equal(router.options.mandatory, true);
    assert.equal(router.options.probe, true);
    assert.equal(stream.options.notify, true);
    assert.equal(pub.options.verbose, true);
    assert.equal(pub.options.verboser, true);
    assert.equal(pub.options.noDrop, true);
    assert.equal(pub.options.manual, true);
    assert.equal(pub.options.manualLastValue, true);
    assert.equal(pub.options.welcomeMessage().data().toString(), 'welcome');
    assert.equal(xpub.options.verbose, true);
    assert.equal(xpub.options.verboser, true);
    assert.equal(xpub.options.noDrop, true);
    assert.equal(xpub.options.manual, true);
    assert.equal(xpub.options.manualLastValue, true);
    assert.equal(xpub.options.welcomeMessage().data().toString(), 'welcome');
    assert.equal(typeof sub.options.topicsCount, 'number');
    sub.close();
    xpub.close();
    pub.close();
    stream.close();
    router.close();
    dealer.close();
    pair.close();
    ctx.close();
});
