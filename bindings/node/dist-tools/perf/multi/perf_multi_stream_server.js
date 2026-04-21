// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { parseMultiArgs } = require('./perf_multi_common');
const { applyContextPolicy, applySocketPolicy } = require('./perf_multi_runtime');
function buildPacketFrame(header, body) {
    const headerBytes = header.data();
    const bodyBytes = body.data();
    const frame = Buffer.allocUnsafe(6 + headerBytes.length + bodyBytes.length);
    frame.writeUInt16BE(headerBytes.length, 0);
    frame.writeUInt32BE(bodyBytes.length, 2);
    headerBytes.copy(frame, 6);
    bodyBytes.copy(frame, 6 + headerBytes.length);
    return frame;
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_STREAM');
    const stream = new zlink.StreamSocket(ctx);
    let rl = null;
    try {
        applySocketPolicy(stream);
        stream.bind(options.endpoint);
        stream.onPacket((routingId, header, body) => {
            stream.send(routingId, buildPacketFrame(header, body));
        });
        console.log(`READY,${options.endpoint}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === 'STOP' || line === 'QUIT') {
                break;
            }
        }
    }
    finally {
        rl?.close();
        stream.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
