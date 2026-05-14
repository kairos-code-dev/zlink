// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const { applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, emitMultiSocketHwmDetail } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_STREAM');
    const stream = new zlink.StreamSocket(ctx);
    let rl = null;
    try {
        applySocketPolicy(stream);
        configureTlsServer(stream, options.transport);
        applyAutoHwmMsgUnit(stream, options.msgSize);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(stream, 'endpoint', options.transport, options.msgSize);
        stream.bind(options.endpoint);
        stream.onPacket((routingId, header, body) => {
            stream.send(routingId).message(body.data()).submit();
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
