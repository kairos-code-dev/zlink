// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
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
        await waitForExit(server);
    }
    finally {
        if (timer) {
            clearTimeout(timer);
        }
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
async function spawnMultiPair(serverScript, clientScript, args) {
    const serverPath = path.join(__dirname, serverScript);
    const clientPath = path.join(__dirname, clientScript);
    const resultLines = [];
    let endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    let sharedArgs = [
        '--endpoint', endpoint,
        '--transport', args.transport,
        '--msg-size', String(args.msgSize),
        '--warmup', String(args.warmup),
        '--duration', String(args.duration),
        '--clients', String(args.clients)
    ];
    if (args.pattern === 'MULTI_SPOT' || args.pattern === 'MULTI_SPOT_REQREP') {
        const peerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        const controlEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        sharedArgs = [
            '--endpoint', endpoint,
            '--transport', args.transport,
            '--peer-endpoint', peerEndpoint,
            '--control-endpoint', controlEndpoint,
            '--msg-size', String(args.msgSize),
            '--warmup', String(args.warmup),
            '--duration', String(args.duration),
            '--clients', String(args.clients)
        ];
    }
    const server = spawn(process.execPath, [serverPath, ...sharedArgs], {
        cwd: process.cwd(),
        stdio: ['pipe', 'pipe', 'pipe'],
        detached: true
    });
    attachProcessCapture(server, resultLines);
    await waitForLine(server, `READY,${endpoint}`, serverScript, 10_000);
    if (args.pattern === 'MULTI_SPOT_REQREP') {
        const routeLine = await waitForPrefix(server, 'ROUTE_READY,', serverScript, 10_000);
        const [, serverNodeRid, serverSpotRid] = routeLine.split(',');
        sharedArgs.push('--server-node-rid', serverNodeRid);
        sharedArgs.push('--server-spot-rid', serverSpotRid);
    }
    const client = spawn(process.execPath, [clientPath, ...sharedArgs], {
        cwd: process.cwd(),
        stdio: ['pipe', 'pipe', 'pipe'],
        detached: true
    });
    attachProcessCapture(client, resultLines);
    await waitForLine(client, clientReadyLine(args.msgSize), clientScript, 20_000);
    if (server.stdin.writable) {
        server.stdin.write(`${startLine(args.msgSize)}\n`);
    }
    if (client.stdin.writable) {
        client.stdin.write(`${startLine(args.msgSize)}\n`);
        if (args.pattern === 'MULTI_DEALER_DEALER' || args.pattern === 'MULTI_PUBSUB') {
            client.stdin.write(`${phaseActiveLine(args.msgSize)}\n`);
        }
    }
    const clientExitCode = await waitForExit(client);
    if (clientExitCode !== 0) {
        await Promise.allSettled([terminateProcessTree(server, 1000)]);
        throw new Error(`client failed (${clientScript}): ${clientExitCode}`);
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
    waitForLine,
    waitForPrefix
};
