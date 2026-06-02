const assert = require('node:assert/strict');
const { createSessionGateway } = require('../session-server/gateway');

async function main() {
  const gateway = createSessionGateway();
  gateway.bind('p1', 'session-1');
  const first = await gateway.manager.getOrCreate('p1', 'player');
  await first.notifyTurn(0);

  gateway.bind('p1', 'session-2');
  const second = await gateway.manager.getOrCreate('p1', 'player');
  await second.notifyTurn(1);
  const staleAccepted = gateway.staleSend('p1', 'session-1');

  assert.equal(first, second);
  assert.equal(staleAccepted, false);
  assert.deepEqual(gateway.bindings.delivered.map((entry) => [entry.token, entry.packetName, entry.payload.cell]), [
    ['session-1', 'TurnPlaced', 0],
    ['session-2', 'TurnPlaced', 1]
  ]);
  console.log('PASS TicTacToe.SessionGateway');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
