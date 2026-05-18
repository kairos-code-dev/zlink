'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
const AUTO_CONNECT_SPOT_MESH = 5;
const AUTO_CONNECT_CLIENT_SERVER = 2;
const AUTO_CONNECT_FANOUT = 4;
const SERVICE_ROLE_SPOT = 2;
const SERVICE_KIND_SPOT_PUB = 4;
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function waitFor(deadlineMs, read) {
    const deadline = Date.now() + deadlineMs;
    while (Date.now() < deadline) {
        const value = read();
        if (value) {
            return value;
        }
        await new Promise((resolve) => setImmediate(resolve));
    }
    return null;
}
test('service objects expose aligned monitor and query surface', () => {
    const ctx = new zlink.Context();
    const registry = new zlink.Registry(ctx);
    const discovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'svc');
    const node = new zlink.SpotNode(ctx);
    const spot = node.createSpot();
    const query = new zlink.RegistryQueryClient(ctx);
    ctx.recalculateAutoHwm();
    registry.bind('inproc://registry-pub', 'inproc://registry-router');
    query.connect('inproc://registry-router');
    discovery.setValue(7);
    assert.equal(discovery.spotOwnerSyncEnabled, false);
    discovery.spotOwnerSyncEnabled = true;
    assert.equal(discovery.spotOwnerSyncEnabled, true);
    assert.equal(discovery.actorRouteSyncEnabled, false);
    discovery.actorRouteSyncEnabled = true;
    assert.equal(discovery.actorRouteSyncEnabled, true);
    assert.equal(registry.statusSnapshot().topologyEntryCount, 0);
    assert.deepEqual(registry.serviceSummarySnapshot(), []);
    assert.deepEqual(registry.topologySnapshot(), []);
    assert.deepEqual(registry.topologyQuery(), []);
    assert.deepEqual(query.snapshot(), []);
    assert.equal(discovery.getValue(), 7);
    assert.deepEqual(discovery.memberPeers(), []);
    assert.equal(discovery.memberPeerMetadata, undefined);
    assert.equal(discovery.bindRoute, undefined);
    assert.equal(discovery.resolveRoute, undefined);
    assert.equal(typeof discovery.resolveSpot, 'function');
    assert.equal(discovery.setDealerPeerMode, undefined);
    assert.equal(typeof spot.onDispatchEvent, 'function');
    assert.equal(spot.drainChannelReplyFrom, undefined);
    assert.equal(node.peersSnapshot().length, 0);
    assert.equal(node.peersQuery().length, 0);
    assert.equal(node.subjectsSnapshot().length, 0);
    node.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('node-id')));
    assert.equal(node.routingId.toBytes().toString(), 'node-id');
    spot.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('spot-id')));
    assert.equal(spot.routingId.toBytes().toString(), 'spot-id');
    const entrySpot = node.entrySpot();
    entrySpot.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('entry-spot-id')));
    const lookedUpSpot = node.spotLookup(zlink.RoutingId.fromBytes(Buffer.from('entry-spot-id')));
    assert.ok(lookedUpSpot);
    assert.equal(lookedUpSpot.routingId.toBytes().toString(), 'entry-spot-id');
    lookedUpSpot.close();
    entrySpot.close();
    assert.equal(zlink.SpotNodeOption, undefined);
    assert.equal(zlink.SpotNodePubMode, undefined);
    assert.equal(zlink.SpotNodePubQueueFullPolicy, undefined);
    assert.equal(discovery.monitorOpen, undefined);
    assert.equal(typeof zlink.MonitorSocket.ignoreHandler, 'function');
    assert.equal(registry.setEndpoints, undefined);
    assert.equal(registry.start, undefined);
    assert.equal(registry.setSockOpt, undefined);
    assert.equal(node.setDiscovery, undefined);
    assert.equal(node.register, undefined);
    assert.equal(node.unregister, undefined);
    assert.equal(node.pubSocket, undefined);
    assert.equal(node.subSocket, undefined);
    assert.equal(node.pubPeers, undefined);
    assert.equal(node.subPeers, undefined);
    assert.throws(() => registry.bind('inproc://registry-pub-2', 'inproc://registry-router-2'), /busy|only be called once/i);
    query.close();
    spot.close();
    node.close();
    discovery.close();
    registry.close();
    ctx.close();
});
test('Spot must be created through SpotNode.createSpot()', () => {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    assert.throws(() => new zlink.Spot(node), /createSpot/);
    node.close();
    ctx.close();
});
test('utility wrappers follow the canonical surface', () => {
    const counter = new zlink.AtomicCounter(3);
    assert.equal(counter.value(), 3);
    assert.equal(counter.inc(), 3);
    assert.equal(counter.value(), 4);
    assert.equal(counter.dec(), 1);
    assert.equal(counter.value(), 3);
    counter.set(0);
    counter.dec();
    assert.equal(counter.value(), -1);
    counter.close();
    const thread = new zlink.Thread(() => { });
    thread.join();
});
test('rid disconnect surface exists on sockets and spot nodes', () => {
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const stream = new zlink.StreamSocket(ctx);
    const node = new zlink.SpotNode(ctx);
    assert.equal(typeof pair.disconnectRid, 'function');
    assert.equal(typeof dealer.disconnectRid, 'function');
    assert.equal(stream.disconnectRid, undefined);
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
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const registry = new zlink.Registry(ctx);
    const discovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'surface-tls');
    const node = new zlink.SpotNode(ctx);
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
    assert.equal(typeof registry.setTlsServer, 'function');
    assert.equal(typeof registry.setTlsClient, 'function');
    assert.equal(typeof discovery.setTlsServer, 'undefined');
    assert.equal(typeof discovery.setTlsClient, 'function');
    assert.equal(typeof node.setTlsServer, 'function');
    assert.equal(typeof node.setTlsClient, 'function');
    assert.throws(() => pair.setTlsServer(Buffer.from('cert'), 'key'), /cert/);
    assert.throws(() => pair.setTlsClient(Buffer.from('ca'), 'host'), /ca/);
    assert.throws(() => registry.setTlsServer(Buffer.from('cert'), 'key'), /cert/);
    assert.throws(() => registry.setTlsClient(Buffer.from('ca'), 'host'), /ca/);
    assert.throws(() => discovery.setTlsClient(Buffer.from('ca'), 'host'), /ca/);
    assert.throws(() => node.setTlsServer(Buffer.from('cert'), 'key'), /cert/);
    assert.throws(() => node.setTlsClient(Buffer.from('ca'), 'host'), /ca/);
    assert.equal(typeof new zlink.Message(Buffer.from('message')).close, 'function');
    assert.equal(typeof zlink.Message.from(Buffer.from('message')).close, 'function');
    node.close();
    discovery.close();
    registry.close();
    dealer.close();
    pair.close();
    ctx.shutdown();
    ctx.close();
});
test('threadSchedulingPolicy getter surfaces ConfigError failures', () => {
    const ctx = new zlink.Context();
    const expected = new zlink.ConfigError(zlink.ConfigResult.NotSupported, 0, 'unsupported');
    const original = ctx.getOptionRawStrictInternal;
    ctx.getOptionRawStrictInternal = () => {
        throw expected;
    };
    assert.throws(() => ctx.options.threadSchedulingPolicy, (error) => error === expected);
    ctx.getOptionRawStrictInternal = original;
    ctx.close();
});
test('native failures surface documented zlink error classes', () => {
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    const monitor = pair.monitorOpen();
    try {
        assert.throws(() => pair.bind('bad://endpoint'), zlink.BindError);
        assert.throws(() => pair.connect('bad://endpoint'), zlink.ConnectError);
        monitor.onEvent(() => { });
        assert.throws(() => monitor.recv(), zlink.RecvError);
        monitor.close();
        assert.throws(() => monitor.snapshot(), zlink.ConfigError);
    }
    finally {
        pair.close();
        ctx.close();
    }
});
test('spot close leaves spot node alive and discovery close tears down attached participants', async () => {
    const ctx = new zlink.Context();
    const standaloneNode = new zlink.SpotNode(ctx);
    const spot = standaloneNode.createSpot();
    const discovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'participant-close');
    const attachedNode = new zlink.SpotNode(ctx);
    const standaloneEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    standaloneNode.bind(standaloneEndpoint);
    assert.ok(standaloneNode.statusSnapshot().nodeRoutingId instanceof zlink.RoutingId);
    attachedNode.attachDiscovery(discovery);
    spot.close();
    assert.equal(standaloneNode.statusSnapshot().configuredPeerCount, 0);
    discovery.close();
    assert.throws(() => attachedNode.statusSnapshot(), /shutdown|closed|transport/i);
    standaloneNode.close();
    ctx.close();
});
test('discovery requires a channel name in the aligned api', () => {
    const ctx = new zlink.Context();
    assert.throws(() => new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH), /channelName/);
    assert.throws(() => new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, ''), /channelName/);
    assert.throws(() => new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'x'.repeat(256)), /255 bytes/);
    ctx.close();
});
test('registry, discovery, and query client expose canonical service discovery flow', async () => {
    const ctx = new zlink.Context();
    const registry = new zlink.Registry(ctx);
    const query = new zlink.RegistryQueryClient(ctx);
    const providerDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'service-found');
    const watcherDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'service-found');
    const node = new zlink.SpotNode(ctx);
    const pubPort = await reservePort();
    const routerPort = await reservePort();
    const servicePort = await reservePort();
    const pubEndpoint = `tcp://127.0.0.1:${pubPort}`;
    const routerEndpoint = `tcp://127.0.0.1:${routerPort}`;
    const serviceEndpoint = `tcp://127.0.0.1:${servicePort}`;
    try {
        registry.bind(pubEndpoint, routerEndpoint);
        query.connect(routerEndpoint);
        providerDiscovery.connectRegistry(routerEndpoint);
        watcherDiscovery.connectRegistry(routerEndpoint);
        node.attachDiscovery(providerDiscovery);
        node.bind(serviceEndpoint);
        const topologyEntry = await waitFor(5000, () => (registry.topologySnapshot().find((entry) => entry.channelName === 'service-found') ?? null));
        assert.ok(topologyEntry);
        assert.equal(topologyEntry.channelName, 'service-found');
        const serviceSummary = await waitFor(5000, () => {
            const [entry] = registry.serviceSummarySnapshot({
                channelName: 'service-found'
            });
            return entry ?? null;
        });
        assert.ok(serviceSummary);
        assert.equal(serviceSummary.channelName, 'service-found');
        const queryEntry = await waitFor(5000, () => (query.snapshot({ channelName: 'service-found' }).find((entry) => (entry.channelName === 'service-found')) ?? null));
        assert.ok(queryEntry);
        assert.equal(queryEntry.channelName, 'service-found');
        const peers = registry.memberPeers('service-found');
        assert.ok(Array.isArray(peers));
        assert.ok(peers.some((peer) => peer.channelName === 'service-found' && peer.endpoint.length > 0));
        const discoveryPeer = await waitFor(5000, () => (watcherDiscovery.memberPeers().find((peer) => peer.endpoint === serviceEndpoint) ?? null));
        assert.ok(discoveryPeer);
        assert.equal(registry.memberPeerMetadata, undefined);
        assert.equal(watcherDiscovery.memberPeerMetadata, undefined);
    }
    finally {
        watcherDiscovery.close();
        providerDiscovery.close();
        query.close();
        registry.close();
        ctx.close();
    }
});
test('attachDiscovery blocks manual lifecycle entry points on canonical sockets and spot nodes', () => {
    const ctx = new zlink.Context();
    const socketDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_FANOUT, 'attached-socket');
    const spotDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'attached-spot');
    const pub = new zlink.PubSocket(ctx);
    const node = new zlink.SpotNode(ctx);
    try {
        pub.attachDiscovery(socketDiscovery);
        node.attachDiscovery(spotDiscovery);
        assert.throws(() => pub.connect('tcp://127.0.0.1:32001'), /busy|state|current|discovery/i);
        assert.throws(() => pub.disconnect('tcp://127.0.0.1:32001'), /busy|state|current|discovery/i);
        assert.throws(() => pub.unbind('tcp://127.0.0.1:32001'), /busy|state|current|discovery/i);
        assert.throws(() => pub.close(), /busy|state|current|discovery/i);
        assert.throws(() => node.connectPeer('tcp://127.0.0.1:32002'), /busy|state|current|discovery/i);
        assert.throws(() => node.disconnectPeer('tcp://127.0.0.1:32002'), /busy|state|current|discovery/i);
    }
    finally {
        spotDiscovery.close();
        socketDiscovery.close();
        ctx.close();
    }
});
test('canonical socket options are exposed through typed facades', () => {
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const router = new zlink.RouterSocket(ctx);
    const stream = new zlink.StreamSocket(ctx);
    const pub = new zlink.PubSocket(ctx);
    const xpub = new zlink.XPubSocket(ctx);
    const sub = new zlink.SubSocket(ctx);
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
    dealer.options.probe = true;
    router.options.mandatory = true;
    pair.options.ridDuplicatePolicy = zlink.RidDuplicatePolicy.Handover;
    router.options.probe = true;
    router.options.setConnectRoutingId(zlink.RoutingId.fromBytes(Buffer.from('route')));
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
