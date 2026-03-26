// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../src');

const ctx = new zlink.Context();
const sub = new zlink.Socket(ctx, zlink.SocketType.SUB);

sub.subscribe('topic');
console.log('subscribed');

sub.close();
ctx.close();
