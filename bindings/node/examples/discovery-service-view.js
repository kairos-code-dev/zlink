// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../dist');

const ctx = new zlink.Context();
const registry = new zlink.Registry(ctx);
const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'example');
const query = new zlink.RegistryQueryClient(ctx);

registry.bind('inproc://example-reg-pub', 'inproc://example-reg-router');
query.connect('inproc://example-reg-router');

console.log(JSON.stringify({
  providers: discovery.memberPeers().length,
  topology: query.snapshot().length
}));

query.close();
discovery.close();
registry.close();
ctx.close();
