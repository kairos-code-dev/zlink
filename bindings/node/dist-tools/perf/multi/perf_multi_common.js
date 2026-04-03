// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
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
async function reservePortInRange(basePort, attempts) {
    for (let offset = 0; offset < attempts; offset += 1) {
        const port = basePort + offset;
        const server = net.createServer();
        try {
            server.listen(port, '127.0.0.1');
            await once(server, 'listening');
            await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
            return port;
        }
        catch (_) {
            server.close(() => { });
        }
    }
    throw new Error(`unable to reserve port in range ${basePort}-${basePort + attempts - 1}`);
}
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
    let sharedArgs;
    let server;
    if (args.pattern === 'MULTI_SPOT') {
        const basePort = 32000 + ((process.pid % 1000) * 8);
        let started = false;
        let lastError = null;
        for (let offset = 0; offset < 64; offset += 1) {
            const port = nextSpotCandidatePort(basePort, offset, 64);
            endpoint = `tcp://127.0.0.1:${port}`;
            sharedArgs = [
                '--endpoint', endpoint,
                '--msg-size', String(args.msgSize),
                '--warmup', String(args.warmup),
                '--duration', String(args.duration),
                '--clients', String(args.clients),
                '--recv', args.recv
            ];
            server = spawn(process.execPath, [serverPath, ...sharedArgs], {
                cwd: process.cwd(),
                stdio: ['pipe', 'pipe', 'pipe'],
                detached: true
            });
            attachProcessCapture(server, resultLines);
            try {
                await waitForLine(server, `READY,${endpoint}`, serverScript, 3000);
                started = true;
                break;
            }
            catch (error) {
                lastError = error;
                await terminateProcessTree(server, 1000);
            }
        }
        if (!started) {
            throw lastError || new Error('failed to start spot server');
        }
    }
    else {
        const port = await reservePort();
        endpoint = `tcp://127.0.0.1:${port}`;
        sharedArgs = [
            '--endpoint', endpoint,
            '--msg-size', String(args.msgSize),
            '--warmup', String(args.warmup),
            '--duration', String(args.duration),
            '--clients', String(args.clients),
            '--recv', args.recv
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
        stdio: ['ignore', 'pipe', 'pipe'],
        detached: true
    });
    attachProcessCapture(client, resultLines);
    const clientReadyLine = args.pattern === 'MULTI_SPOT'
        ? `CLIENT_READY,${args.msgSize}`
        : 'CLIENT_READY';
    await waitForLine(client, clientReadyLine, clientScript, 10000);
    if (args.pattern === 'MULTI_SPOT') {
        await waitForLine(server, `PEERS_READY,${args.msgSize}`, serverScript, 10000);
        server.stdin.write(`START,${args.msgSize}\n`);
    }
    else {
        server.stdin.write('GO\n');
    }
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
    try {
        await stopServer(server, serverScript);
        return resultLines;
    }
    finally {
        await Promise.allSettled([terminateProcessTree(server, 1000), terminateProcessTree(client, 1000)]);
    }
}
module.exports = {
    reservePort,
    spawnMultiPair
};
