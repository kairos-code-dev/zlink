'use strict';

const assert = require('node:assert/strict');
const { pbkdf2 } = require('node:crypto');
const { promisify } = require('node:util');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../../packages/framework/dist/internal');

async function waitFor(condition, label) {
  const deadline = Date.now() + 2000;
  while (Date.now() < deadline) {
    if (condition()) return;
    await new Promise((resolve) => setImmediate(resolve));
  }
  throw new Error(`${label} timed out`);
}

async function main() {
  const context = zlink.createContext();
  const meshName = `framework-publish-commit-${process.pid}`;
  const node = zlink.createMeshNode(context, { meshName });
  node.setRoutingId(zlink.RoutingId.from(meshName));
  node.setBind(`inproc://${meshName}`);
  node.addChannelName('events');
  node.start();
  const subscriber = node.entrySpot();
  subscriber.setSubscription('events', 'pre-start');
  subscriber.setSubscription('events', 'post-start');
  const publisher = node.createPublisher();
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  runtime.publishers.set('play', publisher);

  try {
    const blockWorker = promisify(pbkdf2)(
      Buffer.from('worker-block'),
      Buffer.from('salt'),
      2_000_000,
      16,
      'sha256'
    );
    await new Promise((resolve) => setTimeout(resolve, 20));

    const beforeStart = new AbortController();
    const beforeStartReason = new Error('abort before Core start');
    const cancelled = runtime.publish(
      'play',
      'events',
      'pre-start',
      'ProfileChanged',
      { sequence: 1 },
      beforeStart.signal
    );
    beforeStart.abort(beforeStartReason);
    await assert.rejects(cancelled, (error) => error === beforeStartReason);
    await blockWorker;
    assert.equal(node.status().pendingApplicationMessages, 0n);

    const afterStart = new AbortController();
    const committed = runtime.publish(
      'play',
      'events',
      'post-start',
      'ProfileChanged',
      { sequence: 2 },
      afterStart.signal
    );
    await waitFor(
      () => node.status().pendingApplicationMessages > 0n,
      'Core Logical Multicast admission'
    );
    afterStart.abort(new Error('abort after Core start'));
    const result = await committed;
    assert.equal(result.status, framework.ZLinkSubmitStatus.Submitted);
    assert.equal(result.detail.snapshotLocalSpotCount, 1n);
    assert.equal(result.detail.admittedLocalSpotCount, 1n);
  } finally {
    subscriber.close();
    publisher.close();
    node.shutdown(1000);
    node.close();
    context.close();
  }
}

main().then(
  () => process.stdout.write('logical-multicast-commit-ok\n'),
  (error) => {
    process.stderr.write(`${error?.stack ?? error}\n`);
    process.exitCode = 1;
  }
);
