const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

test('TicTacToe uses a distinct internal room join response contract', () => {
  const messages = fs.readFileSync(path.join(root, 'samples/TicTacToe.Ts/Shared/Contracts/messages.ts'), 'utf8');
  const room = fs.readFileSync(path.join(
    root,
    'samples/TicTacToe.Ts/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe-game-spot.ts'
  ), 'utf8');
  const entry = fs.readFileSync(path.join(
    root,
    'samples/TicTacToe.Ts/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/play-entry-spot.ts'
  ), 'utf8');

  assert.match(messages, /interface TicTacToeGameJoinRes \{[\s\S]*?state: GameState;/);
  assert.match(room, /private admit\([^)]*\): TicTacToeGameJoinRes/);
  assert.match(entry, /submit<Partial<TicTacToeGameJoinRes/);
  assert.match(entry, /return joinGameRes\(reply\.state\)/);
});
