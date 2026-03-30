// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('../dist');
const ctx = new zlink.Context();
const registry = new zlink.Registry(ctx);
const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'example');
const query = new zlink.RegistryQueryClient(ctx);
try {
    registry.bind('inproc://example-reg-pub', 'inproc://example-reg-router');
    query.connect('inproc://example-reg-router');
    discovery.setValue(7);
    discovery.setMetadata('meta');
    assert.equal(discovery.value(), 7);
    assert.equal(discovery.metadata().toString(), 'meta');
    assert.deepEqual(query.snapshot(), []);
    console.log('discovery registry sample ok');
}
finally {
    query.close();
    discovery.close();
    registry.close();
    ctx.close();
}
