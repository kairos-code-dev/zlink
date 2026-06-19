// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const { spawn } = require('node:child_process');
const path = require('node:path');
const zlink = require('@zlink-systems/zlink');
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function waitForPeer(node) {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        const peers = node.peers();
        if (peers.some((peer) => peer.channelName === 'api'
            && peer.kind === zlink.SpotPeerKind.RouterChannel)) {
            await new Promise((resolve) => setTimeout(resolve, 250));
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('spot router channel peer connection timed out');
}
async function waitForSpotPeer(node) {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        if (node.status().connectedPeerCount > 0) {
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('spot node peer connection timed out');
}
function textRoutingId(text) {
    return zlink.RoutingId.from(Buffer.from(text, 'ascii'));
}
test('router requestToSpot promise resolves through spot routed reply', async () => {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = zlink.createContext();
    const responderNode = zlink.createSpotNode(ctx);
    const requester = zlink.createRouterSocket(ctx);
    const responder = responderNode.createSpot();
    try {
        requester.bind(endpoint);
        responderNode.connectRouterChannelPeer('api', endpoint);
        await waitForPeer(responderNode);
        const handled = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const received = new zlink.Received();
            const poll = () => {
                try {
                    if (!responder.recvRouted(received, zlink.RecvFlags.DontWait)) {
                        if (Date.now() < deadline) {
                            setImmediate(poll);
                            return;
                        }
                        reject(new Error('timed out waiting for routed request'));
                        return;
                    }
                    try {
                        assert.ok(received.routingId);
                        assert.equal(received.spotRid, null);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), 'spot-ping');
                        received.reply().message(Buffer.from('spot-pong')).submit();
                        resolve(null);
                    }
                    finally {
                        received.close();
                    }
                }
                catch (error) {
                    if (error instanceof zlink.RecvError && Date.now() < deadline) {
                        setImmediate(poll);
                        return;
                    }
                    reject(error);
                }
            };
            poll();
        });
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId).message(Buffer.from('spot-ping')).timeout(2000).submit();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'spot-pong');
        await handled;
    }
    finally {
        responder.close();
        requester.close();
        responderNode.close();
        ctx.close();
    }
});
test('spot requestToSpot promise resolves through peer spot routed reply', async () => {
    const serverPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverCtx = zlink.createContext();
    const clientCtx = zlink.createContext();
    const serverNode = zlink.createSpotNode(serverCtx);
    const clientNode = zlink.createSpotNode(clientCtx);
    const serverSpot = serverNode.createSpot();
    const clientSpot = clientNode.createSpot();
    try {
        serverNode.setRoutingId(textRoutingId('REQREP_SERVER_NODE'));
        clientNode.setRoutingId(textRoutingId('REQREP_CLIENT_NODE'));
        serverSpot.setRoutingId(textRoutingId('REQREP_SERVER_SPOT'));
        clientSpot.setRoutingId(textRoutingId('REQREP_CLIENT_SPOT'));
        serverNode.setRouterBind(serverRouterEndpoint);
        serverNode.setPubBind(serverPeerEndpoint);
        clientNode.setRouterBind(clientRouterEndpoint);
        clientNode.setPubBind(clientPeerEndpoint);
        serverNode.connectPeer(clientPeerEndpoint);
        clientNode.connectPeer(serverPeerEndpoint);
        await Promise.all([
            waitForSpotPeer(serverNode),
            waitForSpotPeer(clientNode)
        ]);
        const handled = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const received = new zlink.Received();
            const poll = () => {
                try {
                    if (!serverSpot.recvRouted(received, zlink.RecvFlags.DontWait)) {
                        if (Date.now() < deadline) {
                            setImmediate(poll);
                            return;
                        }
                        reject(new Error('timed out waiting for peer spot request'));
                        return;
                    }
                    try {
                        assert.ok(received.routingId);
                        assert.ok(received.spotRid);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), 'mesh-ping');
                        received.reply().message(Buffer.from('mesh-pong')).submit();
                        resolve(null);
                    }
                    finally {
                        received.close();
                    }
                }
                catch (error) {
                    if (error instanceof zlink.RecvError && Date.now() < deadline) {
                        setImmediate(poll);
                        return;
                    }
                    reject(error);
                }
            };
            poll();
        });
        const reply = await clientSpot.requestToSpot(serverNode.routingId, serverSpot.routingId).message(Buffer.from('mesh-ping')).timeout(2000).submit();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'mesh-pong');
        await handled;
    }
    finally {
        clientSpot.close();
        serverSpot.close();
        clientNode.close();
        serverNode.close();
        clientCtx.close();
        serverCtx.close();
    }
});
test('spot requestToSpot promise resolves through peer entry spot routed reply', async () => {
    const serverPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverCtx = zlink.createContext();
    const clientCtx = zlink.createContext();
    const serverNode = zlink.createSpotNode(serverCtx);
    const clientNode = zlink.createSpotNode(clientCtx);
    const serverEntrySpot = serverNode.entrySpot();
    const clientSpot = clientNode.createSpot();
    try {
        serverNode.setRoutingId(textRoutingId('REQREP_ENTRY_SERVER_NODE'));
        clientNode.setRoutingId(textRoutingId('REQREP_ENTRY_CLIENT_NODE'));
        clientSpot.setRoutingId(textRoutingId('REQREP_ENTRY_CLIENT_SPOT'));
        serverNode.setRouterBind(serverRouterEndpoint);
        serverNode.setPubBind(serverPeerEndpoint);
        clientNode.setRouterBind(clientRouterEndpoint);
        clientNode.setPubBind(clientPeerEndpoint);
        serverNode.connectPeer(clientPeerEndpoint);
        clientNode.connectPeer(serverPeerEndpoint);
        await Promise.all([
            waitForSpotPeer(serverNode),
            waitForSpotPeer(clientNode)
        ]);
        const handled = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const received = new zlink.Received();
            const poll = () => {
                try {
                    if (!serverEntrySpot.recvRouted(received, zlink.RecvFlags.DontWait)) {
                        if (Date.now() < deadline) {
                            setImmediate(poll);
                            return;
                        }
                        reject(new Error('timed out waiting for peer entry spot request'));
                        return;
                    }
                    try {
                        assert.ok(received.routingId);
                        assert.ok(received.spotRid);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), 'entry-ping');
                        received.reply().message(Buffer.from('entry-pong')).submit();
                        resolve(null);
                    }
                    finally {
                        received.close();
                    }
                }
                catch (error) {
                    if (error instanceof zlink.RecvError && Date.now() < deadline) {
                        setImmediate(poll);
                        return;
                    }
                    reject(error);
                }
            };
            poll();
        });
        const reply = await clientSpot.requestToSpot(serverNode.routingId, serverEntrySpot.routingId).message(Buffer.from('entry-ping')).timeout(2000).submit();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'entry-pong');
        await handled;
    }
    finally {
        clientSpot.close();
        serverNode.close();
        clientNode.close();
        serverCtx.close();
        clientCtx.close();
    }
});
test('spot requestToSpot from spot created after peer connect resolves through peer entry spot routed reply', async () => {
    const serverPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverCtx = zlink.createContext();
    const clientCtx = zlink.createContext();
    const serverNode = zlink.createSpotNode(serverCtx);
    const clientNode = zlink.createSpotNode(clientCtx);
    const serverEntrySpot = serverNode.entrySpot();
    let clientSpot;
    try {
        serverNode.setRoutingId(textRoutingId('REQREP_LATE_ENTRY_SERVER_NODE'));
        clientNode.setRoutingId(textRoutingId('REQREP_LATE_ENTRY_CLIENT_NODE'));
        serverNode.setRouterBind(serverRouterEndpoint);
        serverNode.setPubBind(serverPeerEndpoint);
        clientNode.setRouterBind(clientRouterEndpoint);
        clientNode.setPubBind(clientPeerEndpoint);
        serverNode.connectPeer(clientPeerEndpoint);
        clientNode.connectPeer(serverPeerEndpoint);
        await Promise.all([
            waitForSpotPeer(serverNode),
            waitForSpotPeer(clientNode)
        ]);
        clientSpot = clientNode.getOrCreateSpot(textRoutingId('REQREP_LATE_ENTRY_CLIENT_SPOT')).spot;
        const handled = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const received = new zlink.Received();
            const poll = () => {
                try {
                    if (!serverEntrySpot.recvRouted(received, zlink.RecvFlags.DontWait)) {
                        if (Date.now() < deadline) {
                            setImmediate(poll);
                            return;
                        }
                        reject(new Error('timed out waiting for late peer entry spot request'));
                        return;
                    }
                    try {
                        assert.ok(received.routingId);
                        assert.ok(received.spotRid);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), 'late-entry-ping');
                        received.reply().message(Buffer.from('late-entry-pong')).submit();
                        resolve(null);
                    }
                    finally {
                        received.close();
                    }
                }
                catch (error) {
                    if (error instanceof zlink.RecvError && Date.now() < deadline) {
                        setImmediate(poll);
                        return;
                    }
                    reject(error);
                }
            };
            poll();
        });
        const reply = await clientSpot.requestToSpot(serverNode.routingId, serverEntrySpot.routingId).message(Buffer.from('late-entry-ping')).timeout(2000).submit();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'late-entry-pong');
        await handled;
    }
    finally {
        clientSpot?.close();
        serverNode.close();
        clientNode.close();
        serverCtx.close();
        clientCtx.close();
    }
});
test('spot requestToSpot resolves across child processes', async () => {
    const serverPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const serverRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const fixturesDir = path.join(__dirname, 'fixtures');
    const serverPath = path.join(fixturesDir, 'spot_reqrep_child_server.js');
    const clientPath = path.join(fixturesDir, 'spot_reqrep_child_client.js');
    const server = spawn(process.execPath, [
        serverPath,
        '--peer-endpoint', serverPeerEndpoint,
        '--router-endpoint', serverRouterEndpoint,
        '--connect-endpoint', clientPeerEndpoint
    ], {
        cwd: path.join(__dirname, '..'),
        stdio: ['ignore', 'pipe', 'pipe'],
        detached: true
    });
    const client = spawn(process.execPath, [
        clientPath,
        '--peer-endpoint', clientPeerEndpoint,
        '--router-endpoint', clientRouterEndpoint,
        '--connect-endpoint', serverPeerEndpoint
    ], {
        cwd: path.join(__dirname, '..'),
        stdio: ['ignore', 'pipe', 'pipe'],
        detached: true
    });
    let serverStderr = '';
    let clientStderr = '';
    server.stderr.on('data', (chunk) => {
        serverStderr += chunk.toString();
    });
    client.stderr.on('data', (chunk) => {
        clientStderr += chunk.toString();
    });
    const waitForServerLine = createChildLineWaiter(server);
    const waitForClientLine = createChildLineWaiter(client);
    try {
        await waitForServerLine('SERVER_READY', 5000, () => serverStderr);
        await waitForClientLine('CLIENT_READY', 5000, () => clientStderr);
        await waitForServerLine('SERVER_REPLIED', 5000, () => serverStderr);
        await waitForClientLine('CLIENT_REPLY,mesh-pong', 5000, () => clientStderr);
    }
    finally {
        for (const child of [server, client]) {
            try {
                process.kill(-child.pid, 'SIGKILL');
            }
            catch (_) {
            }
        }
    }
});
function createChildLineWaiter(child) {
    const queued = [];
    const waiters = [];
    let buffered = '';
    let exited = false;
    let exitCode = null;
    const tryResolve = (waiter) => {
        const index = queued.indexOf(waiter.expected);
        if (index < 0) {
            return false;
        }
        queued.splice(0, index + 1);
        clearTimeout(waiter.timeout);
        waiter.resolve();
        return true;
    };
    const flushWaiters = () => {
        for (let index = 0; index < waiters.length;) {
            if (tryResolve(waiters[index])) {
                waiters.splice(index, 1);
            }
            else {
                index += 1;
            }
        }
    };
    child.stdout.on('data', (chunk) => {
        buffered += chunk.toString();
        while (true) {
            const newline = buffered.indexOf('\n');
            if (newline === -1) {
                break;
            }
            const line = buffered.slice(0, newline).trim();
            buffered = buffered.slice(newline + 1);
            if (line) {
                queued.push(line);
            }
        }
        flushWaiters();
    });
    child.once('exit', (code) => {
        exited = true;
        exitCode = code;
        for (const waiter of waiters.splice(0)) {
            clearTimeout(waiter.timeout);
            waiter.reject(new Error(`exited before ${waiter.expected}: ${exitCode}: ${waiter.sink()}`));
        }
    });
    return (expected, timeoutMs, sink) => new Promise((resolve, reject) => {
        const waiter = {
            expected,
            sink,
            resolve,
            reject,
            timeout: setTimeout(() => {
                const index = waiters.indexOf(waiter);
                if (index >= 0) {
                    waiters.splice(index, 1);
                }
                reject(new Error(`timeout waiting for ${expected}: ${sink()}`));
            }, timeoutMs)
        };
        if (tryResolve(waiter)) {
            return;
        }
        if (exited) {
            clearTimeout(waiter.timeout);
            reject(new Error(`exited before ${expected}: ${exitCode}: ${sink()}`));
            return;
        }
        waiters.push(waiter);
    });
}
