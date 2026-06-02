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
    assert.equal(result.staleAccepted, false);
    assert.deepEqual(result.delivered.map((entry) => [entry.sessionId, entry.packetName, entry.cell]), [
      ['session-1', 'TurnPlaced', 0],
      ['session-2', 'TurnPlaced', 1]
    ]);
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
