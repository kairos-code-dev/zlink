// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { tcpEndpoint } = require('./sample_support');
async function main() {
    // --8<-- [start:doc]
    const ctx = zlink.createContext();
    const roomNode = zlink.createSpotNode(ctx);
    const room = roomNode.createSpot();
    const roomDealer = zlink.createDealerSocket(ctx);
    const apiRouter = zlink.createRouterSocket(ctx);
    const bridge = roomNode.createRouteBridge();
    try {
        const channel = 'api';
        const endpoint = await tcpEndpoint();
        apiRouter.bind(endpoint);
        roomDealer.connect(endpoint);
        bridge.attachDealerChannel(channel, roomDealer);
        // 게임룸 노드의 route bridge가 API 채널로 outgame 요청을 보낸다.
        const replyPromise = bridge.request(channel, room.routingId)
            .message(Buffer.from('get-profile'))
            .timeout(5000)
            .submit();
        // API 서버(ROUTER)는 요청을 폴링으로 받아 응답한다.
        await new Promise((resolve, reject) => {
            const received = new zlink.Received();
            const poll = () => {
                try {
                    if (apiRouter.recv(received, zlink.RecvFlags.DontWait)) {
                        apiRouter.reply(received.routingId, received.requestSeq)
                            .message(Buffer.from('profile:level-7'))
                            .submit();
                        received.close();
                        resolve();
                        return;
                    }
                }
                catch (error) {
                    if (!(error instanceof zlink.RecvError)) {
                        reject(error);
                        return;
                    }
                }
                setImmediate(poll);
            };
            poll();
        });
        const reply = await replyPromise;
        console.log(`[spot/channel] request "get-profile" -> reply "${reply[0].data().toString()}"`);
    }
    finally {
        bridge.close();
        apiRouter.close();
        roomDealer.close();
        room.close();
        roomNode.close();
        ctx.close();
    }
    // --8<-- [end:doc]
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
