'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src');

test('version matches core', () => {
  assert.deepEqual(zlink.version(), [5, 0, 4]);
});

test('legacy recv(size, flags) remains as compatibility path', () => {
  const ctx = new zlink.Context();
  const sender = new zlink.Socket(ctx, zlink.SocketType.PAIR);
  const receiver = new zlink.Socket(ctx, zlink.SocketType.PAIR);

  sender.bind('inproc://legacy-recv-contract');
  receiver.connect('inproc://legacy-recv-contract');
  sender.send('ping');

  const out = receiver.recv(16, 0);
  assert.equal(out.toString(), 'ping');

  receiver.close();
  sender.close();
  ctx.close();
});

test('recvInto reuses caller-provided buffer', () => {
  const ctx = new zlink.Context();
  const sender = new zlink.Socket(ctx, zlink.SocketType.PAIR);
  const receiver = new zlink.Socket(ctx, zlink.SocketType.PAIR);

  sender.bind('inproc://recv-into-contract');
  receiver.connect('inproc://recv-into-contract');
  sender.send(zlink.Message.copyOf('pong'));

  const target = Buffer.alloc(16);
  const received = receiver.recvInto(target);
  assert.equal(received, 4);
  assert.equal(target.subarray(0, received).toString(), 'pong');

  receiver.close();
  sender.close();
  ctx.close();
});

test('dedicated option helpers cover routing id and generic option access', () => {
  const ctx = new zlink.Context();
  const dealer = new zlink.Socket(ctx, zlink.SocketType.DEALER);
  const routingId = Buffer.from('dealer-1');

  dealer.setRoutingId(routingId);
  assert.equal(dealer.getRoutingId().subarray(0, routingId.length).toString(), 'dealer-1');

  dealer.close();
  ctx.close();
});

test('stream attach exposes explicit unsupported contract and detach remains safe', () => {
  const ctx = new zlink.Context();
  const stream = new zlink.Socket(ctx, zlink.SocketType.STREAM);

  assert.throws(
    () => stream.streamAttach(() => 0, zlink.StreamDispatchMode.LEN32BE),
    /not available on the aligned public API/
  );
  assert.doesNotThrow(() => stream.streamDetach());

  stream.close();
  ctx.close();
});
