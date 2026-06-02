const assert = require('node:assert/strict');
const path = require('node:path');
const { startRoleProcess } = require('../../shared/role-process');

async function main() {
  const registry = await startRoleProcess(path.resolve(__dirname, '../registry-server/main.js'));
  const session = await startRoleProcess(path.resolve(__dirname, '../session-server/main.js'));
  const play = await startRoleProcess(path.resolve(__dirname, '../play-server/main.js'));
  const api = await startRoleProcess(path.resolve(__dirname, '../api-server/main.js'));

  try {
    assert.equal((await registry.request({ command: 'ping' })).role, 'registry-server');
    assert.equal((await session.request({ command: 'ping' })).role, 'session-server');
    assert.equal((await play.request({ command: 'ping' })).role, 'play-server');

    const result = await api.request({ command: 'run' });
    assert.equal(result.winner, 'p1');
    assert.deepEqual(result.notifications, [{
      actorId: 'p1',
      packetName: 'BingoWinner',
      payload: { winner: 'p1', number: 7 }
    }]);
  } finally {
    await api.close();
    await play.close();
    await session.close();
    await registry.close();
  }

  console.log('PASS Bingo');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
