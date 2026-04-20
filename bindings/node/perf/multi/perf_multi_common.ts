// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { once } = require('node:events');
const { MIN_MSG_SIZE } = require('../common/perf_metrics');

function parseArgs(argv, defaults = {}) {
  const options = {
    endpoint: '',
    peerEndpoint: '',
    controlEndpoint: '',
    serverControlEndpoint: '',
    transport: 'tcp',
    serverNodeRid: '',
    serverSpotRid: '',
    msgSize: 256,
    duration: 5,
    clients: 1,
    ...defaults
  };

  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--peer-endpoint') {
      options.peerEndpoint = argv[++i];
    } else if (argv[i] === '--control-endpoint') {
      options.controlEndpoint = argv[++i];
    } else if (argv[i] === '--server-control-endpoint') {
      options.serverControlEndpoint = argv[++i];
    } else if (argv[i] === '--transport') {
      options.transport = String(argv[++i] || '').trim().toLowerCase();
    } else if (argv[i] === '--server-node-rid') {
      options.serverNodeRid = argv[++i];
    } else if (argv[i] === '--server-spot-rid') {
      options.serverSpotRid = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    } else if (argv[i] === '--clients') {
      options.clients = Number(argv[++i]);
    }
  }

  if (!Number.isFinite(options.msgSize) || options.msgSize < MIN_MSG_SIZE) {
    throw new Error(`invalid multi msg size: ${options.msgSize}`);
  }
  if (!Number.isFinite(options.duration) || options.duration <= 0) {
    throw new Error(`invalid multi duration: ${options.duration}`);
  }
  if (!Number.isFinite(options.clients) || options.clients <= 0) {
    throw new Error(`invalid multi clients: ${options.clients}`);
  }

  return options;
}

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return address.port;
}

async function benchmarkEndpoint(transport, token, bindPort = 0) {
  if (transport === 'ipc') {
    return `ipc://${path.join(os.tmpdir(), `zlink-node-multi-perf-${process.pid}-${token}.sock`)}`;
  }
  if (transport === 'tcp' || transport === 'tls' || transport === 'ws' || transport === 'wss') {
    const port = Number.isFinite(bindPort) && bindPort > 0
      ? Math.trunc(bindPort)
      : await reservePort();
    return `${transport}://127.0.0.1:${port}`;
  }
  throw new Error(`unsupported multi transport: ${transport}`);
}

function resolveMultiConnectConcurrency(clientCount) {
  const configured = Number(process.env.PERF_MULTI_CONNECT_CONCURRENCY);
  if (Number.isFinite(configured) && configured > 0) {
    return Math.trunc(configured);
  }
  return clientCount >= 10000 ? 1024 : 128;
}

function resolveMultiSpotReadySettleMs() {
  const configured = Number(process.env.PERF_MULTI_SPOT_READY_SETTLE_MS);
  return Number.isFinite(configured) && configured >= 0 ? Math.trunc(configured) : 1000;
}

function resolveMultiSpotControlSettleMs() {
  const configured = Number(process.env.PERF_MULTI_SPOT_CONTROL_SETTLE_MS);
  return Number.isFinite(configured) && configured >= 0 ? Math.trunc(configured) : 25;
}

module.exports = {
  benchmarkEndpoint,
  parseMultiArgs: parseArgs,
  reservePort,
  resolveMultiConnectConcurrency,
  resolveMultiSpotControlSettleMs,
  resolveMultiSpotReadySettleMs
};
