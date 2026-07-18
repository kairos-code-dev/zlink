// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
// 완료는 push 콜백이 아니라 pull dispatch(ready index)로 도착한다.
// (한 프로세스에 두 노드를 두려면 노드마다 별도 context — 각자 IO 스레드 — 가 필요하다.)
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { MeshPump, MeshRecordKind, isRequestRecord, operationIdEquals, tcpEndpoint, waitPeerAdmitted, waitUntil } = require('./sample_support');
async function main() {
    // --8<-- [start:doc]
    const serverCtx = zlink.createContext();
    const clientCtx = zlink.createContext();
    const serverNode = zlink.createMeshNode(serverCtx, { meshName: 'samples' });
    const clientNode = zlink.createMeshNode(clientCtx, { meshName: 'samples' });
    let serverPump = null;
    let clientPump = null;
    let server = null;
    let client = null;
    try {
        const serverEndpoint = await tcpEndpoint();
        const clientEndpoint = await tcpEndpoint();
        serverNode.setBind(serverEndpoint);
        clientNode.setBind(clientEndpoint);
        serverNode.start();
        clientNode.start();
        serverNode.connectPeer({ endpoint: clientEndpoint });
        clientNode.connectPeer({ endpoint: serverEndpoint });
        server = serverNode.createSpot();
        client = clientNode.createSpot();
        serverPump = new MeshPump(serverNode);
        clientPump = new MeshPump(clientNode);
        await waitPeerAdmitted(serverNode);
        await waitPeerAdmitted(clientNode);
        // 서버 Spot의 주소(노드 rid + spot rid + 세대)를 클라이언트가 요청 대상으로 쓴다.
        const serverNodeRid = serverNode.status().routingId;
        const serverSpotRid = server.routingId;
        const serverSpotGeneration = server.status().lifecycleGeneration;
        // 클라이언트 Spot이 서버 Spot으로 요청한다.
        const operationId = client.requestToSpot(serverNodeRid, serverSpotRid, serverSpotGeneration, Buffer.from('ping'), { timeoutMs: 3000 });
        // 두 노드를 번갈아 pump한다: 서버는 요청을 받아 응답하고, 클라이언트는 완료를 받는다.
        let completion = null;
        await waitUntil(() => {
            serverPump.drain((record) => {
                if (isRequestRecord(record))
                    record.reply(Buffer.from('pong'));
            });
            clientPump.drain((record) => {
                if (record.kind === MeshRecordKind.Completion
                    && operationIdEquals(record.operationId, operationId)) {
                    completion = record;
                }
            });
            return completion !== null;
        }, 5000, 'routed request did not complete');
        assert.equal(completion.terminalResult, zlink.RequestResult.Ok);
        console.log(`[spot/rpc] request "ping" -> reply "${completion.parts[0].data().toString()}"`);
    }
    finally {
        if (serverPump)
            serverPump.close();
        if (clientPump)
            clientPump.close();
        if (server)
            server.close();
        if (client)
            client.close();
        serverNode.close();
        clientNode.close();
        serverCtx.close();
        clientCtx.close();
    }
    // --8<-- [end:doc]
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
