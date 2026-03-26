// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../src');

const ctx = new zlink.Context();
const spot = new zlink.Spot(ctx);

spot.subscribe('topic');
spot.subscribePattern('topic.*');
spot.unsubscribe('topic');
console.log('spot subscriptions configured');

spot.close();
ctx.close();
