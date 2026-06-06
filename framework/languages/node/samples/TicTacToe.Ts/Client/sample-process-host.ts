const childProcess = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const readline = require('node:readline');

async function reservePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const { port } = server.address();
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function reserveTcpEndpoint() {
  return `tcp://127.0.0.1:${await reservePort()}`;
}

async function startServer(entryFile, env = {}) {
  const stdoutLines = [];
  const stderrLines = [];
  let stopping = false;
  const child = childProcess.spawn(process.execPath, [path.resolve(entryFile)], {
    env: { ...process.env, ...env },
    stdio: ['ignore', 'pipe', 'pipe']
  });
  child.stderr.on('data', (chunk) => {
    const text = chunk.toString();
    stderrLines.push(text);
    process.stderr.write(text);
  });
  const output = readline.createInterface({ input: child.stdout, crlfDelay: Infinity });
  output.on('line', (line) => stdoutLines.push(line));
  const exit = new Promise<any>((resolve) => {
    child.once('exit', (code, signal) => resolve({ code, signal }));
  });
  const ready = await readReady(output, child, entryFile, stdoutLines, stderrLines);
  return {
    ready,
    entryFile,
    async exitedUnexpectedly() {
      const result = await exit;
      if (stopping) {
        return undefined;
      }
      return new Error(`${entryFile} exited while sample was running: code=${result.code} signal=${result.signal}\nstdout:\n${stdoutLines.join('\n')}\nstderr:\n${stderrLines.join('')}`);
    },
    async stop() {
      stopping = true;
      if (child.exitCode !== null) {
        output.close();
        return;
      }
      child.kill('SIGINT');
      const closed = await waitForClose(child, 1000);
      if (!closed && child.exitCode === null) {
        child.kill('SIGKILL');
        await waitForClose(child, 1000);
      }
      output.close();
    }
  };
}

async function withServers(servers, action) {
  const started = [];
  try {
    for (const server of servers) {
      started.push(await startServer(server.entry, server.env));
    }
    const actionResult = action(started.map((server) => server.ready));
    const unexpectedExit = Promise.race(
      started.map((server) => server.exitedUnexpectedly())
    ).then((error) => {
      if (error) {
        throw error;
      }
      return new Promise(() => {});
    });
    return await Promise.race([
      actionResult,
      unexpectedExit,
      rejectAfterDeadline(15000, () => false)
    ]);
  } finally {
    for (const server of started.reverse()) {
      await server.stop();
    }
  }
}

function readReady(output, child, entryFile, stdoutLines, stderrLines) {
  return new Promise((resolve, reject) => {
    let settled = false;
    void rejectAfterDeadline(5000, () => settled).then(() => {
      if (!settled) {
        settled = true;
        reject(new Error(`Timed out waiting for ${entryFile} ready event.\nstdout:\n${stdoutLines.join('\n')}\nstderr:\n${stderrLines.join('')}`));
      }
    });
    child.once('exit', (code, signal) => {
      if (settled) {
        return;
      }
      settled = true;
      reject(new Error(`${entryFile} exited before ready: code=${code} signal=${signal}\nstdout:\n${stdoutLines.join('\n')}\nstderr:\n${stderrLines.join('')}`));
    });
    output.once('line', (line) => {
      try {
        const message = JSON.parse(line);
        if (message.event === 'ready') {
          settled = true;
          resolve(message);
        } else {
          settled = true;
          reject(new Error(`Expected ready event, got ${line}`));
        }
      } catch (error) {
        settled = true;
        reject(error);
      }
    });
  });
}

async function waitForClose(child, timeoutMs) {
  let closed = false;
  child.once('close', () => {
    closed = true;
  });
  await rejectAfterDeadline(timeoutMs, () => closed).catch(() => undefined);
  return closed;
}

async function rejectAfterDeadline(timeoutMs, done) {
  const deadline = Date.now() + timeoutMs;
  while (!done() && Date.now() < deadline) {
    await new Promise((resolve) => setImmediate(resolve));
  }
  if (!done()) {
    throw new Error('deadline reached');
  }
}

module.exports = { reserveTcpEndpoint, startServer, withServers };
