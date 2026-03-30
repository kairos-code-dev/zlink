// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const { once } = require('node:events');
const { spawn } = require('node:child_process');
const path = require('node:path');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return address.port;
}

function collectLines(stream, onLine) {
  let buffered = '';
  stream.setEncoding('utf8');
  stream.on('data', (chunk) => {
    buffered += chunk;
    while (true) {
      const newline = buffered.indexOf('\n');
      if (newline === -1) {
        break;
      }
      const line = buffered.slice(0, newline).trim();
      buffered = buffered.slice(newline + 1);
      if (line) {
        onLine(line);
      }
    }
  });
}

async function spawnMultiPair(serverScript, clientScript, args) {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const serverPath = path.join(__dirname, serverScript);
  const clientPath = path.join(__dirname, clientScript);
  const sharedArgs = [
    '--endpoint', endpoint,
    '--msg-size', String(args.msgSize),
    '--warmup', String(args.warmup),
    '--duration', String(args.duration),
    '--clients', String(args.clients),
    '--recv', args.recv
  ];

  const server = spawn(process.execPath, [serverPath, ...sharedArgs], {
    cwd: process.cwd(),
    stdio: ['pipe', 'pipe', 'pipe']
  });

  const resultLines = [];
  let readySeen = false;

  collectLines(server.stdout, (line) => {
    if (line === `READY,${endpoint}`) {
      readySeen = true;
      return;
    }
    if (line.startsWith('RESULT,')) {
      resultLines.push(line);
      return;
    }
    console.log(line);
  });
  collectLines(server.stderr, (line) => {
    console.error(line);
  });

  const serverReady = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error(`server ready timeout: ${serverScript}`)), 5000);
    const poll = setInterval(() => {
      if (readySeen) {
        clearInterval(poll);
        clearTimeout(timeout);
        resolve();
      }
    }, 10);
    server.once('exit', (code) => {
      clearInterval(poll);
      clearTimeout(timeout);
      if (!readySeen) {
        reject(new Error(`server exited before READY (${serverScript}): ${code}`));
      }
    });
  });

  await serverReady;

  const client = spawn(process.execPath, [clientPath, ...sharedArgs], {
    cwd: process.cwd(),
    stdio: ['ignore', 'pipe', 'pipe']
  });
  collectLines(client.stdout, (line) => {
    if (line === 'CLIENT_READY') {
      server.stdin.write('GO\n');
      return;
    }
    if (line.startsWith('RESULT,')) {
      resultLines.push(line);
      return;
    }
    console.log(line);
  });
  collectLines(client.stderr, (line) => {
    console.error(line);
  });

  const [clientCode] = await once(client, 'exit');
  if (clientCode !== 0) {
    throw new Error(`client failed (${clientScript}): ${clientCode}`);
  }

  server.stdin.write('STOP\n');
  server.stdin.end();
  const [serverCode] = await once(server, 'exit');
  if (serverCode !== 0) {
    throw new Error(`server failed (${serverScript}): ${serverCode}`);
  }

  return resultLines;
}

module.exports = {
  reservePort,
  spawnMultiPair
};
