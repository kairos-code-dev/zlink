// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
// (단일 이벤트루프에서 한 프로세스에 두 노드를 두려면 노드마다 별도 context가
//  필요하고, 서버측 라우티드 수신은 setImmediate 폴링으로 처리한다.)

'use strict';

const net = require('node:net');
const { once } = require('node:events');
const zlink = require('@zlink-systems/zlink');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function waitPeer(node) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    if (node.status().connectedPeerCount > 0) return;
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  throw new Error('spot peer not connected');
}

function rid(text) {
  return zlink.RoutingId.from(Buffer.from(text));
}

async function main() {
// --8<-- [start:doc]
  const serverCtx = zlink.createContext();
  const clientCtx = zlink.createContext();
  const serverNode = zlink.createSpotNode(serverCtx);
  const clientNode = zlink.createSpotNode(clientCtx);
  const server = serverNode.createSpot();
  const client = clientNode.createSpot();

  try {
    serverNode.setRoutingId(rid('rpc-server-node'));
    clientNode.setRoutingId(rid('rpc-client-node'));
    server.setRoutingId(rid('rpc-server-spot'));
    client.setRoutingId(rid('rpc-client-spot'));
    // 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
    serverNode.setRouterBind(`tcp://127.0.0.1:${await reservePort()}`);
    clientNode.setRouterBind(`tcp://127.0.0.1:${await reservePort()}`);
    const serverEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const clientEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    serverNode.setPubBind(serverEndpoint);
    clientNode.setPubBind(clientEndpoint);
    serverNode.connectPeer(clientEndpoint);
    clientNode.connectPeer(serverEndpoint);

    await waitPeer(serverNode);
    await waitPeer(clientNode);

    // 서버 Spot은 라우티드 요청을 폴링으로 받아 같은 평면으로 응답한다.
    const served = new Promise((resolve, reject) => {
      const deadline = Date.now() + 5000;
      const received = new zlink.Received();
      const poll = () => {
        try {
          if (!server.recvRouted(received, zlink.RecvFlags.DontWait)) {
            if (Date.now() < deadline) { setImmediate(poll); return; }
            reject(new Error('timed out waiting for routed request'));
            return;
          }
          received.reply().message(Buffer.from('pong')).submit();
          received.close();
          resolve();
        } catch (error) {
          if (error instanceof zlink.RecvError && Date.now() < deadline) {
            setImmediate(poll);
            return;
          }
          reject(error);
        }
      };
      poll();
    });

    // 클라이언트 Spot이 서버 Spot으로 요청한다.
    const reply = await client.requestToSpot(rid('rpc-server-node'), rid('rpc-server-spot'))
      .message(Buffer.from('ping'))
      .timeout(3000)
      .submit();
    await served;
    console.log(`[spot/rpc] request "ping" -> reply "${reply[0].data().toString()}"`);
  } finally {
    server.close();
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
