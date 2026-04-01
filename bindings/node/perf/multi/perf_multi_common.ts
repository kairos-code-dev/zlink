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

async function waitForLine(processRef, expected, label, timeoutMs) {
  return new Promise((resolve, reject) => {
    let done = false;
    const timeout = setTimeout(() => {
      if (!done) {
        done = true;
        reject(new Error(`${label} timeout waiting for ${expected}`));
      }
    }, timeoutMs);
    processRef.once('exit', (code) => {
      if (!done) {
        done = true;
        clearTimeout(timeout);
        reject(new Error(`${label} exited before ${expected}: ${code}`));
      }
    });
    processRef.__waiters.push((line) => {
      if (!done && line === expected) {
        done = true;
        clearTimeout(timeout);
        resolve();
        return true;
      }
      return false;
    });
  });
}

function attachProcessCapture(child, resultLines) {
  child.__waiters = [];
  collectLines(child.stdout, (line) => {
    for (const waiter of child.__waiters) {
      if (waiter(line)) {
        return;
      }
    }
    if (line.startsWith('RESULT,')) {
      resultLines.push(line);
      return;
    }
    console.log(line);
  });
  collectLines(child.stderr, (line) => {
    console.error(line);
  });
}

async function stopServer(server, label, timeoutMs = 5000) {
  if (server.stdin.writable) {
    server.stdin.write('STOP\n');
    server.stdin.end();
  }
  let timer = null;
  try {
    const graceful = await Promise.race([
      once(server, 'exit'),
      new Promise((resolve) => {
        timer = setTimeout(() => resolve(null), timeoutMs);
      })
    ]);
    if (graceful !== null) {
      const [code] = graceful;
      if (code !== 0) {
        throw new Error(`${label} failed: ${code}`);
      }
      return;
    }

    server.kill('SIGTERM');
    const terminated = await Promise.race([
      once(server, 'exit'),
      new Promise((resolve) => {
        timer = setTimeout(() => resolve(null), timeoutMs);
      })
    ]);
    if (terminated !== null) {
      return;
    }

    server.kill('SIGKILL');
    await once(server, 'exit');
  } finally {
    if (timer) {
      clearTimeout(timer);
    }
  }
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

  const resultLines = [];
  const server = spawn(process.execPath, [serverPath, ...sharedArgs], {
    cwd: process.cwd(),
    stdio: ['pipe', 'pipe', 'pipe']
  });
  attachProcessCapture(server, resultLines);
  await waitForLine(server, `READY,${endpoint}`, serverScript, 5000);

  const client = spawn(process.execPath, [clientPath, ...sharedArgs], {
    cwd: process.cwd(),
    stdio: ['ignore', 'pipe', 'pipe']
  });
  attachProcessCapture(client, resultLines);
  await waitForLine(client, 'CLIENT_READY', clientScript, 5000);

  server.stdin.write('GO\n');
  const [clientCode] = await once(client, 'exit');
  if (clientCode !== 0) {
    throw new Error(`client failed (${clientScript}): ${clientCode}`);
  }

  await stopServer(server, serverScript);
  return resultLines;
}

module.exports = {
  reservePort,
  spawnMultiPair
};
