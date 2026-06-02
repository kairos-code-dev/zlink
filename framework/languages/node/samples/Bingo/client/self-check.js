const assert = require('node:assert/strict');
const { createBingoServer } = require('../api-server/bingo-server');

async function main() {
  const server = createBingoServer();
  const room = await server.spots.create(server.BingoRoomSpot);
  let winnerTask;
  await server.spots.executeOnSpot(room.spotRid, (spot) => {
    spot.join('p1', [7], server.sessionFor('p1'));
    spot.join('p2', [9], server.sessionFor('p2'));
    winnerTask = spot.start([7, 9]);
  });
  const winner = await winnerTask;

  assert.equal(winner, 'p1');
  assert.deepEqual(server.notificationHub.messages, [{
    actorId: 'p1',
    packetName: 'BingoWinner',
    payload: { winner: 'p1', number: 7 }
  }]);
  console.log('PASS Bingo');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
