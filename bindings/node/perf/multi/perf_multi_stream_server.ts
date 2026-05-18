// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  emitMultiSocketHwmDetail
} = require('./perf_multi_runtime');

function buildStreamPacketFrame(header, body) {
  const headerData = header.data();
  const bodyData = body.data();
  const frame = Buffer.allocUnsafe(6 + headerData.length + bodyData.length);
  frame.writeUInt16BE(headerData.length, 0);
  frame.writeUInt32BE(bodyData.length, 2);
  headerData.copy(frame, 6);
  bodyData.copy(frame, 6 + headerData.length);
  return frame;
}

function isTransientStreamSubmit(error) {
  const text = String(error && error.message ? error.message : error);
  return (error instanceof zlink.SubmitError
      && (error.result === zlink.SubmitResult.Backpressured
        || error.result === zlink.SubmitResult.NotConnected
        || error.result === zlink.SubmitResult.NotFound))
    || (error && error.code === 'EAGAIN')
    || /Resource temporarily unavailable|temporarily unavailable|would block|not connected|Host unreachable|reset/i.test(text);
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_STREAM');
  const stream = new zlink.StreamSocket(ctx);
  let rl = null;

  try {
    applySocketPolicy(stream);
    configureTlsServer(stream, options.transport);
    applyAutoHwmMsgUnit(ctx, options.msgSize);
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(stream, 'endpoint', options.transport, options.msgSize);
    stream.bind(options.endpoint);
    stream.onPacket((routingId, header, body) => {
      try {
        stream.send(routingId).message(buildStreamPacketFrame(header, body)).submit();
      } catch (error) {
        if (!isTransientStreamSubmit(error)) {
          throw error;
        }
      }
    });
    console.log(`READY,${options.endpoint}`);

    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === 'STOP' || line === 'QUIT') {
        break;
      }
    }
  } finally {
    rl?.close();
    stream.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
