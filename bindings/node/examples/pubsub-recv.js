// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../dist');

const ctx = new zlink.Context();
const sub = new zlink.SubSocket(ctx);

sub.subscribe('topic');
console.log('subscribed');

sub.close();
ctx.close();
