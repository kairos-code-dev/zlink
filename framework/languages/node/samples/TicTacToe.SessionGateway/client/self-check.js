const assert = require('node:assert/strict');
const path = require('node:path');
const { withRoleProcess } = require('../../shared/role-process');

async function main() {
  await withRoleProcess(
    path.resolve(__dirname, '../session-server/main.js'),
    { command: 'run' },
    async (result) => {
      assert.equal(result.sameActor, true);
      assert.equal(result.staleAccepted, false);
      assert.deepEqual(result.delivered.map((entry) => [entry.sessionId, entry.packetName, entry.cell]), [
        ['session-1', 'TurnPlaced', 0],
        ['session-2', 'TurnPlaced', 1]
      ]);
    }
  );
    console.log('PASS TicTacToe.SessionGateway');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
