// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const { once } = require('node:events');

function parseArgs(argv, defaults = {}) {
  const options = {
    endpoint: '',
    controlEndpoint: '',
    msgSize: 256,
    warmup: 1,
    duration: 2,
    clients: 1,
    ...defaults
  };

  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--control-endpoint') {
      options.controlEndpoint = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--warmup') {
      options.warmup = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    } else if (argv[i] === '--clients') {
      options.clients = Number(argv[++i]);
    }
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

module.exports = {
  parseMultiArgs: parseArgs,
  reservePort
};
