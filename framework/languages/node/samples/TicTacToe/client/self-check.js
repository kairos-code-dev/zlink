const assert = require('node:assert/strict');
const { createGameServer } = require('../server/game-server');

async function main() {
  const server = createGameServer();
  const match = await server.channelClient
    .requestToChannel('match', Buffer.from('start'))
    .packetName('CreateMatch')
    .timeout(1000)
    .submit();
  const created = await server.spots.create(server.GameSpot);
  const p1 = await server.actors.getOrCreate('p1', 'player');
  const p2 = await server.actors.getOrCreate('p2', 'player');

  let winner;
  await server.spots.executeOnSpot(created.spotRid, (spot) => {
    spot.join(p1.actorId, 'X');
    spot.join(p2.actorId, 'O');
    spot.place('p1', 0);
    spot.place('p2', 3);
    spot.place('p1', 1);
    spot.place('p2', 4);
    winner = spot.place('p1', 2);
  });

  assert.equal(match.toString(), 'match-ready');
  assert.equal(winner, 'p1');
  assert.equal(server.channelEvents[0].packetName, 'CreateMatch');
  console.log('PASS TicTacToe');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
