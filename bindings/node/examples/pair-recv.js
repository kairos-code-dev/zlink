// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../src');

const ctx = new zlink.Context();
const server = new zlink.Socket(ctx, zlink.SocketType.PAIR);
const client = new zlink.Socket(ctx, zlink.SocketType.PAIR);

server.bind('inproc://example-pair-recv');
client.connect('inproc://example-pair-recv');
client.send(zlink.Message.copyOf('example'));

const received = server.recv();
console.log(received.parts[0].toString());

client.close();
server.close();
ctx.close();
