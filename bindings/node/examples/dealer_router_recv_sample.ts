// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const zlink = require('../dist');

const ctx = new zlink.Context();
const router = new zlink.RouterSocket(ctx);
const dealer = new zlink.DealerSocket(ctx);

try {
  router.bind('inproc://example-dealer-router');
  dealer.connect('inproc://example-dealer-router');
  dealer.send(zlink.Message.copyOf('hello'));

  const request = router.receive();
  assert.equal(request.parts[0].toBuffer().toString(), 'hello');
  assert.ok(Buffer.isBuffer(request.routingId));
  router.send(request.routingId, zlink.Message.copyOf('world'));

  const response = dealer.receive();
  assert.equal(response.parts[0].toBuffer().toString(), 'world');
  console.log('dealer router recv sample ok');
} finally {
  dealer.close();
  router.close();
  ctx.close();
}
