// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const { once } = require('node:events');
const { spawn } = require('node:child_process');
const path = require('node:path');
const { reservePort } = require('./perf_multi_common');
function nextSpotCandidatePort(basePort, offset, attempts) {
    return basePort + (offset % attempts);
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
function attachProcessCapture(child, resultLines) {
    child.__waiters = [];
    child.__seenLines = [];
    collectLines(child.stdout, (line) => {
        child.__seenLines.push(line);
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
    }
    catch (error) {
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
    }
    catch (error) {
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
        try {
            process.kill(-server.pid, 'SIGTERM');
        }
        catch (error) {
            if (!error || error.code !== 'ESRCH') {
                throw error;
            }
        }
        const terminated = await Promise.race([
            waitForExit(server).then((code) => [code]),
            new Promise((resolve) => {
                timer = setTimeout(() => resolve(null), timeoutMs);
            })
        ]);
        if (terminated !== null) {
            return;
        }
        try {
            process.kill(-server.pid, 'SIGKILL');
        }
        catch (error) {
            if (!error || error.code !== 'ESRCH') {
                throw error;
            }
        }
        await waitForExit(server);
    }
    finally {
        if (timer) {
            clearTimeout(timer);
        }
    }
}
async function spawnMultiPair(serverScript, clientScript, args) {
    const serverPath = path.join(__dirname, serverScript);
    const clientPath = path.join(__dirname, clientScript);
    const resultLines = [];
    let endpoint;
    let controlEndpoint;
    let sharedArgs;
    let server;
    if (args.pattern === 'MULTI_SPOT') {
        endpoint = `tcp://127.0.0.1:${await reservePort()}`;
        controlEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        sharedArgs = [
            '--endpoint', endpoint,
            '--control-endpoint', controlEndpoint,
            '--msg-size', String(args.msgSize),
            '--warmup', String(args.warmup),
            '--duration', String(args.duration),
            '--clients', String(args.clients)
        ];
        server = spawn(process.execPath, [serverPath, ...sharedArgs], {
            cwd: process.cwd(),
            stdio: ['pipe', 'pipe', 'pipe'],
            detached: true
        });
        collectLines(server.stdout, (line) => {
            if (!line.startsWith('CONTROL_CONNECTED,')) {
                return;
            }
            if (client && client.stdin.writable) {
                client.stdin.write(`${line}\n`);
            }
        });
        attachProcessCapture(server, resultLines);
        await waitForLine(server, `READY,${endpoint}`, serverScript, 5000);
        await waitForLine(server, `CONTROL_READY,${controlEndpoint}`, serverScript, 5000);
    }
    else {
        const port = await reservePort();
        endpoint = `tcp://127.0.0.1:${port}`;
        sharedArgs = [
            '--endpoint', endpoint,
            '--msg-size', String(args.msgSize),
            '--warmup', String(args.warmup),
            '--duration', String(args.duration),
            '--clients', String(args.clients)
        ];
        server = spawn(process.execPath, [serverPath, ...sharedArgs], {
            cwd: process.cwd(),
            stdio: ['pipe', 'pipe', 'pipe'],
            detached: true
        });
        attachProcessCapture(server, resultLines);
        await waitForLine(server, `READY,${endpoint}`, serverScript, 5000);
    }
    const client = spawn(process.execPath, [clientPath, ...sharedArgs], {
        cwd: process.cwd(),
        stdio: [args.pattern === 'MULTI_SPOT' ? 'pipe' : 'ignore', 'pipe', 'pipe'],
        detached: true
    });
    if (args.pattern === 'MULTI_SPOT') {
        collectLines(client.stdout, (line) => {
            if (!line.startsWith('CLIENT_CONTROL_ENDPOINT,')) {
                return;
            }
            const endpoint = line.slice('CLIENT_CONTROL_ENDPOINT,'.length);
            if (endpoint && server.stdin.writable) {
                server.stdin.write(`CONNECT_CONTROL,${endpoint}\n`);
            }
        });
    }
    attachProcessCapture(client, resultLines);
    const clientReadyLine = args.pattern === 'MULTI_SPOT'
        ? `CLIENT_READY,${args.msgSize}`
        : 'CLIENT_READY';
    await waitForLine(client, clientReadyLine, clientScript, 10000);
    server.stdin.write(`START,${args.msgSize}\n`);
    const firstExit = await Promise.race([
        waitForExit(client).then((code) => ({ side: 'client', code })),
        waitForExit(server).then((code) => ({ side: 'server', code }))
    ]);
    if (firstExit.side === 'server') {
        await Promise.allSettled([terminateProcessTree(client, 1000)]);
        throw new Error(`server failed early (${serverScript}): ${firstExit.code}`);
    }
    if (firstExit.code !== 0) {
        await Promise.allSettled([terminateProcessTree(server, 1000), terminateProcessTree(client, 1000)]);
        throw new Error(`client failed (${clientScript}): ${firstExit.code}`);
    }
    await flushProcessOutput();
    try {
        await stopServer(server, serverScript);
        await flushProcessOutput();
        return resultLines;
    }
    finally {
        await Promise.allSettled([terminateProcessTree(server, 1000), terminateProcessTree(client, 1000)]);
    }
}
module.exports = {
    attachProcessCapture,
    spawnMultiPair,
    stopServer,
    waitForLine
};
