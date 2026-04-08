// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const net = require('node:net');
const { once } = require('node:events');
function parseArgs(argv, defaults = {}) {
    const options = {
        endpoint: '',
        msgSize: 256,
        warmup: 1,
        duration: 2,
        clients: 1,
        recv: 'recv',
        ...defaults
    };
    for (let i = 0; i < argv.length; i += 1) {
        if (argv[i] === '--endpoint') {
            options.endpoint = argv[++i];
        }
        else if (argv[i] === '--msg-size') {
            options.msgSize = Number(argv[++i]);
        }
        else if (argv[i] === '--warmup') {
            options.warmup = Number(argv[++i]);
        }
        else if (argv[i] === '--duration') {
            options.duration = Number(argv[++i]);
        }
        else if (argv[i] === '--clients') {
            options.clients = Number(argv[++i]);
        }
        else if (argv[i] === '--recv') {
            options.recv = argv[++i];
        }
    }
    return options;
}
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const address = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return address.port;
}
async function waitForSpotSubscriberReady(slot, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (slot.node.statusSnapshot().readySubjectCount > 0) {
            return;
        }
        await new Promise((resolve) => setImmediate(resolve));
    }
    throw new Error(`spot client ready timeout: ${JSON.stringify({
        status: slot.node.statusSnapshot(),
        peers: slot.node.peersSnapshot(),
        subjects: slot.node.subjectsSnapshot()
    })}`);
}
module.exports = {
    parseMultiArgs: parseArgs,
    reservePort,
    waitForSpotSubscriberReady
};
