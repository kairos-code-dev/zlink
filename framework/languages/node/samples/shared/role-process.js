const childProcess = require('node:child_process');
const { once } = require('node:events');
const readline = require('node:readline');
const path = require('node:path');

async function runRoleServer(createState, handlers) {
  const state = await createState();
  const input = readline.createInterface({
    input: process.stdin,
    crlfDelay: Infinity
  });

  send({ event: 'ready' });
  for await (const line of input) {
    if (line.trim().length === 0) {
      continue;
    }
    const message = JSON.parse(line);
    if (message.command === 'shutdown') {
      input.close();
      break;
    }
    const handler = handlers[message.command];
    if (handler === undefined) {
      send({ event: 'error', message: `Unsupported command: ${message.command}` });
      continue;
    }
    send({ event: 'result', result: await handler(state, message) });
  }
}

async function withRoleProcess(entryFile, command, verify) {
  const role = childProcess.spawn(process.execPath, [path.resolve(entryFile)], {
    stdio: ['pipe', 'pipe', 'inherit']
  });
  const output = readline.createInterface({
    input: role.stdout,
    crlfDelay: Infinity
  });

  try {
    const ready = await readEvent(output);
    if (ready.event !== 'ready') {
      throw new Error(`Expected ready event but received '${ready.event}'.`);
    }
    role.stdin.write(`${JSON.stringify(command)}\n`);
    const result = await readEvent(output);
    if (result.event !== 'result') {
      throw new Error(`Expected result event but received '${result.event}'.`);
    }
    await verify(result.result);
  } finally {
    role.stdin.write(`${JSON.stringify({ command: 'shutdown' })}\n`);
    role.stdin.end();
    await once(role, 'close');
    output.close();
  }
}

function send(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function readEvent(output) {
  return new Promise((resolve, reject) => {
    output.once('line', (line) => {
      const message = JSON.parse(line);
      if (message.event === 'error') {
        reject(new Error(message.message));
      } else {
        resolve(message);
      }
    });
  });
}

module.exports = { runRoleServer, withRoleProcess };
