// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { pbkdf2 } = require('node:crypto');
const { promisify } = require('node:util');
const zlink = require('../../dist');
async function main() {
    const blockWorker = promisify(pbkdf2)(Buffer.from('worker-block'), Buffer.from('salt'), 2_000_000, 16, 'sha256');
    await new Promise((resolve) => setTimeout(resolve, 20));
    const context = zlink.createContext();
    const meshName = `publisher-cancel-${process.pid}`;
    const node = zlink.createMeshNode(context, { meshName });
    node.setRoutingId(zlink.RoutingId.from(`publisher-cancel-${process.pid}`));
    node.setBind(`inproc://${meshName}`);
    node.addChannelName('events');
    node.start();
    const subscriber = node.entrySpot();
    subscriber.setSubscription('events', 'cancel-before-start');
    const publisher = node.createPublisher();
    try {
        const controller = new AbortController();
        const reason = new Error('cancel queued publish');
        const pending = publisher.publishAsync('events', 'cancel-before-start', Buffer.from('must-not-reach-core'), undefined, controller.signal);
        controller.abort(reason);
        await assert.rejects(pending, (error) => error === reason);
        await blockWorker;
        assert.equal(node.status().pendingApplicationMessages, 0n);
        assert.equal(node.shutdown(1000), zlink.RequestResult.Ok);
    }
    finally {
        subscriber.close();
        publisher.close();
        node.close();
        context.close();
    }
}
main().then(() => process.stdout.write('publisher-cancel-ok\n'), (error) => {
    process.stderr.write(`${error?.stack ?? error}\n`);
    process.exitCode = 1;
});
