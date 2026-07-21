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
    const meshName = `publisher-close-gc-${process.pid}`;
    const node = zlink.createMeshNode(context, { meshName });
    node.setRoutingId(zlink.RoutingId.from(`publisher-close-gc-${process.pid}`));
    node.setBind(`inproc://${meshName}`);
    node.addChannelName('events');
    node.start();
    const subscriber = node.entrySpot();
    subscriber.setSubscription('events', 'close-gc');
    let publisher = node.createPublisher();
    try {
        const pending = publisher.publishAsync('events', 'close-gc', Buffer.from('payload'));
        publisher.close();
        await assert.rejects(publisher.publishAsync('events', 'close-gc', Buffer.from('late')));
        publisher = null;
        let eventLoopTicks = 0;
        for (let index = 0; index < 4; index += 1) {
            globalThis.gc?.();
            await new Promise((resolve) => setImmediate(resolve));
            eventLoopTicks += 1;
        }
        assert.equal(eventLoopTicks, 4);
        await blockWorker;
        const outcome = await pending;
        assert.equal(outcome.result, zlink.SubmitResult.Ok);
        assert.equal(outcome.detail.admittedLocalSpotCount, 1);
        subscriber.close();
        assert.equal(node.shutdown(1000), zlink.RequestResult.Ok);
        node.close();
        context.close();
    }
    catch (error) {
        subscriber.close();
        publisher?.close();
        node.close();
        context.close();
        throw error;
    }
}
main().then(() => process.stdout.write('publisher-close-gc-ok\n'), (error) => {
    process.stderr.write(`${error?.stack ?? error}\n`);
    process.exitCode = 1;
});
