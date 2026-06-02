const assert = require('node:assert/strict');
const path = require('node:path');
const { startRoleProcess } = require('../../shared/role-process');

async function main() {
  const registry = await startRoleProcess(path.resolve(__dirname, '../registry-server/main.js'));
  const api = await startRoleProcess(path.resolve(__dirname, '../api-server/main.js'));
  const play = await startRoleProcess(path.resolve(__dirname, '../play-server/main.js'));
  const session = await startRoleProcess(path.resolve(__dirname, '../session-server/main.js'));

  try {
    assert.equal((await registry.request({ command: 'ping' })).role, 'registry-server');
    assert.equal((await api.request({ command: 'ping' })).role, 'api-server');
    assert.equal((await play.request({ command: 'ping' })).role, 'play-server');

    const result = await session.request({ command: 'run' });
    assert.equal(result.sameActor, true);
    assert.equal(result.opponentActor, 'p2');
    assert.equal(result.staleAccepted, false);
    assert.deepEqual(result.delivered.slice(0, 2).map((entry) => [entry.sessionId, entry.packetName, entry.cell]), [
      ['session-1', 'TurnPlaced', 0],
      ['session-2', 'TurnPlaced', 1]
    ]);
    assert.equal(result.finalState.board, 'XXXOO....');
    assert.equal(result.finalState.status, 'Won');
    assert.equal(result.finalState.winnerActorId, 'p1');
    assert.deepEqual(result.moves.map((state) => state.board), [
      'X........',
      'X..O.....',
      'XX.O.....',
      'XX.OO....',
      'XXXOO....'
    ]);

    const pushedByPacket = result.delivered.reduce((counts, entry) => {
      counts[entry.packetName] = (counts[entry.packetName] ?? 0) + 1;
      return counts;
    }, {});
    assert.equal(pushedByPacket.OpponentJoinedNotify, 2);
    assert.equal(pushedByPacket.TurnChangedNotify, 10);
    assert.equal(pushedByPacket.GameEndedNotify, 2);
  } finally {
    await session.close();
    await play.close();
    await api.close();
    await registry.close();
  }

  console.log('PASS TicTacToe.SessionGateway');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
