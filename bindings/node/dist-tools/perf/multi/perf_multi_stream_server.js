// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { parseMultiArgs } = require('./perf_multi_common');
function frame(buffer) {
    const framed = Buffer.allocUnsafe(buffer.length + 4);
    framed.writeUInt32BE(buffer.length, 0);
    buffer.copy(framed, 4);
    return framed;
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2), { msgSize: undefined, warmup: undefined, duration: undefined, clients: undefined });
    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    let stop = false;
    const processPacket = (routingId, header, body) => {
        const payload = body && body.data().length > 0 ? body.data() : header.data();
        stream.send(routingId, Buffer.from(frame(payload)));
    };
    try {
        stream.bind(options.endpoint);
        stream.onPacket(processPacket);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === 'STOP') {
                stop = true;
                stream.close();
                break;
            }
        }
    }
    finally {
        stream.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
