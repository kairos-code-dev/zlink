// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../dist');

const ctx = new zlink.Context();
const router = new zlink.RouterSocket(ctx);
const dealer = new zlink.DealerSocket(ctx);

router.bind('inproc://example-dealer-router');
dealer.connect('inproc://example-dealer-router');
dealer.send(zlink.Message.copyOf('hello'));

const request = router.recv();
console.log(request.parts[0].toString());
router.sendTo(request.routingId, zlink.Message.copyOf('world'));

const response = dealer.recv();
console.log(response.parts[0].toString());

dealer.close();
router.close();
ctx.close();
