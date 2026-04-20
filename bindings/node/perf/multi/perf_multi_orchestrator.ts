// SPDX-License-Identifier: MPL-2.0

'use strict';

const { once } = require('node:events');
const { spawn } = require('node:child_process');
const path = require('node:path');
const { reservePort } = require('./perf_multi_common');

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
    if (processRef.__seenLines.includes(expected)) {
      resolve();
      return;
    }
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

async function waitForPrefix(processRef, prefix, label, timeoutMs) {
  return new Promise((resolve, reject) => {
    let done = false;
    const seen = processRef.__seenLines.find((line) => line.startsWith(prefix));
    if (seen) {
      resolve(seen);
      return;
    }
    const timeout = setTimeout(() => {
      if (!done) {
        done = true;
        reject(new Error(`${label} timeout waiting for ${prefix}`));
      }
    }, timeoutMs);
    processRef.once('exit', (code) => {
      if (!done) {
        done = true;
        clearTimeout(timeout);
        reject(new Error(`${label} exited before ${prefix}: ${code}`));
      }
    });
    processRef.__waiters.push((line) => {
      if (!done && line.startsWith(prefix)) {
        done = true;
        clearTimeout(timeout);
        resolve(line);
        return true;
      }
      return false;
    });
  });
}

function attachProcessCapture(child, resultLines, resultPrefix = 'RESULT,') {
  child.__waiters = [];
  child.__seenLines = [];
  child.__stderrLines = [];
  collectLines(child.stdout, (line) => {
    child.__seenLines.push(line);
    for (const waiter of child.__waiters) {
      if (waiter(line)) {
        return;
      }
    }
    if (line.startsWith(resultPrefix) || line.startsWith('UNSUPPORTED,') || line.startsWith('SKIP,')) {
      resultLines.push(line);
      return;
    }
    console.log(line);
  });
  collectLines(child.stderr, (line) => {
    child.__stderrLines.push(line);
    console.error(line);
  });
}

async function waitForExit(processRef) {
  if (processRef.exitCode !== null || processRef.signalCode !== null) {
    return processRef.exitCode;
  }
  const [code] = await once(processRef, 'exit');
  return code;
}

async function flushProcessOutput() {
  await new Promise((resolve) => setTimeout(resolve, 50));
}

async function terminateProcessTree(processRef, timeoutMs = 5000) {
  if (processRef.exitCode !== null || processRef.signalCode !== null) {
    return;
  }
  try {
    process.kill(-processRef.pid, 'SIGTERM');
  } catch (error) {
    if (!error || error.code !== 'ESRCH') {
      throw error;
    }
    return;
  }

  const graceful = await Promise.race([
    waitForExit(processRef).then(() => true),
    new Promise((resolve) => setTimeout(() => resolve(false), timeoutMs))
  ]);
  if (graceful) {
    return;
  }

  try {
    process.kill(-processRef.pid, 'SIGKILL');
  } catch (error) {
    if (!error || error.code !== 'ESRCH') {
      throw error;
    }
    return;
  }
  await waitForExit(processRef);
}

async function stopServer(server, label, timeoutMs = 5000) {
  if (server.stdin.writable) {
    server.stdin.write('STOP\n');
    server.stdin.end();
  }
  try {
    const graceful = await Promise.race([
      once(server, 'exit'),
      new Promise((resolve) => setTimeout(() => resolve(null), timeoutMs))
    ]);
    if (graceful !== null) {
      const [code] = graceful;
      if (code !== 0) {
        throw new Error(`${label} failed: ${code}`);
      }
      return;
    }

    try {
      process.kill(-server.pid, 'SIGTERM');
    } catch (error) {
      if (!error || error.code !== 'ESRCH') {
        throw error;
      }
    }
    const terminated = await Promise.race([
      waitForExit(server).then(() => true),
      new Promise((resolve) => setTimeout(() => resolve(false), timeoutMs))
    ]);
    if (terminated) {
      return;
    }
    try {
      process.kill(-server.pid, 'SIGKILL');
    } catch (error) {
      if (!error || error.code !== 'ESRCH') {
        throw error;
      }
    }
    await waitForExit(server);
  } catch (error) {
    const stderrText = Array.isArray(server.__stderrLines) ? server.__stderrLines.join('\n') : '';
    if (stderrText) {
      error.message = `${error.message}\n${stderrText}`;
    }
    throw error;
  }
}

function clientReadyLine(msgSize) {
  return `CLIENT_READY,${msgSize}`;
}

function phaseActiveLine(msgSize) {
  return `PHASE_ACTIVE,${msgSize}`;
}

function startLine(msgSize) {
  return `START,${msgSize}`;
}

function childEnv(args) {
  const env = { ...process.env };
  if (Number.isFinite(args.hwm)) {
    env.PERF_MULTI_HWM = String(args.hwm);
  }
  if (Number.isFinite(args.sendHwm)) {
    env.PERF_MULTI_SNDHWM = String(args.sendHwm);
  }
  if (Number.isFinite(args.recvHwm)) {
    env.PERF_MULTI_RCVHWM = String(args.recvHwm);
  }
  if (Number.isFinite(args.sendTimeoutMs)) {
    env.PERF_MULTI_SNDTIMEO_MS = String(args.sendTimeoutMs);
  }
  if (Number.isFinite(args.recvTimeoutMs)) {
    env.PERF_MULTI_RCVTIMEO_MS = String(args.recvTimeoutMs);
  }
  if (Number.isFinite(args.connectReadyTimeoutMs)) {
    env.PERF_MULTI_CONNECT_READY_TIMEOUT_MS = String(args.connectReadyTimeoutMs);
  }
  if (Number.isFinite(args.monitorHwm)) {
    env.PERF_MULTI_MONITOR_HWM = String(args.monitorHwm);
  }
  return env;
}

async function spawnMultiPair(serverScript, clientScript, args) {
  const serverPath = path.join(__dirname, serverScript);
  const clientPath = path.join(__dirname, clientScript);
  const resultLines = [];
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  let serverArgs = [
    '--endpoint', endpoint,
    '--transport', args.transport,
    '--msg-size', String(args.msgSize),
    '--duration', String(args.duration),
    '--clients', String(args.clients)
  ];
  let clientArgs = [...serverArgs];

  if (args.pattern === 'MULTI_SPOT' || args.pattern === 'MULTI_SPOT_REQREP') {
    const serverControlEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientControlEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    serverArgs = [
      '--endpoint', endpoint,
      '--transport', args.transport,
      '--peer-endpoint', endpoint,
      '--control-endpoint', serverControlEndpoint,
      '--msg-size', String(args.msgSize),
      '--duration', String(args.duration),
      '--clients', String(args.clients)
    ];
    clientArgs = [
      '--endpoint', endpoint,
      '--transport', args.transport,
      '--peer-endpoint', endpoint,
      '--control-endpoint', clientControlEndpoint,
      '--server-control-endpoint', serverControlEndpoint,
      '--msg-size', String(args.msgSize),
      '--duration', String(args.duration),
      '--clients', String(args.clients)
    ];
  }

  const server = spawn(process.execPath, [serverPath, ...serverArgs], {
    cwd: process.cwd(),
    env: childEnv(args),
    stdio: ['pipe', 'pipe', 'pipe'],
    detached: true
  });
  attachProcessCapture(server, resultLines);
  await waitForLine(
    server,
    `READY,${endpoint}`,
    serverScript,
    Number.isFinite(args.serverReadyTimeoutMs) ? args.serverReadyTimeoutMs : 10_000
  );
  if (args.pattern === 'MULTI_SPOT' || args.pattern === 'MULTI_SPOT_REQREP') {
    await waitForPrefix(server, 'CONTROL_READY,', serverScript, 10_000);
  }

  if (args.pattern === 'MULTI_SPOT_REQREP') {
    const routeLine = await waitForPrefix(server, 'ROUTE_READY,', serverScript, 10_000);
    const [, serverNodeRid, serverSpotRid] = routeLine.split(',');
    clientArgs.push('--server-node-rid', serverNodeRid);
    clientArgs.push('--server-spot-rid', serverSpotRid);
  }

  const client = spawn(process.execPath, [clientPath, ...clientArgs], {
    cwd: process.cwd(),
    env: childEnv(args),
    stdio: ['pipe', 'pipe', 'pipe'],
    detached: true
  });
  attachProcessCapture(client, resultLines);
  if (args.pattern === 'MULTI_SPOT' || args.pattern === 'MULTI_SPOT_REQREP') {
    const clientControlLine = await waitForPrefix(client, 'CLIENT_CONTROL_ENDPOINT,', clientScript, 20_000);
    await waitForPrefix(client, 'CONTROL_CONNECTED,', clientScript, 20_000);
    if (server.stdin.writable) {
      server.stdin.write(`CONNECT_CONTROL,${clientControlLine.split(',')[1]}\n`);
    }
  }
  await waitForLine(
    client,
    clientReadyLine(args.msgSize),
    clientScript,
    Number.isFinite(args.connectReadyTimeoutMs) ? args.connectReadyTimeoutMs : 20_000
  );

  if (server.stdin.writable) {
    server.stdin.write(`${startLine(args.msgSize)}\n`);
  }
  if (client.stdin.writable) {
    client.stdin.write(`${startLine(args.msgSize)}\n`);
    if (args.pattern === 'MULTI_PUBSUB') {
      client.stdin.write(`${phaseActiveLine(args.msgSize)}\n`);
    }
  }

  const clientExitCode = await waitForExit(client);
  if (clientExitCode !== 0) {
    const stderrText = Array.isArray(client.__stderrLines) ? client.__stderrLines.join('\n') : '';
    await Promise.allSettled([terminateProcessTree(server, 1000)]);
    const error = new Error(`client failed (${clientScript}): ${clientExitCode}`);
    if (stderrText) {
      error.message = `${error.message}\n${stderrText}`;
    }
    throw error;
  }
  await flushProcessOutput();

  try {
    await stopServer(
      server,
      serverScript,
      Number.isFinite(args.serverShutdownTimeoutMs) ? args.serverShutdownTimeoutMs : 5000
    );
    await flushProcessOutput();
    return resultLines;
  } finally {
    await Promise.allSettled([terminateProcessTree(server, 1000), terminateProcessTree(client, 1000)]);
  }
}

module.exports = {
  attachProcessCapture,
  spawnMultiPair,
  stopServer,
  waitForLine,
  waitForPrefix
};
