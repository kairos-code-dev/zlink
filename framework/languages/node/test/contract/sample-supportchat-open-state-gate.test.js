const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');

function read(relativePath) {
  return fs.readFileSync(path.join(nodeRoot, relativePath), 'utf8');
}

test('SupportChat preserves the domain conversation state through the open response chain', () => {
  const contracts = read('samples/SupportChat.Ts/Shared/Contracts/messages.ts');
  const allocator = read(
    'samples/SupportChat.Ts/Server/Support/Infrastructure/ZLink/Handlers/allocate-conversation-handler.ts'
  );
  const entry = read(
    'samples/SupportChat.Ts/Server/Support/Infrastructure/ZLink/Spots/EntrySpot/support-entry-handlers.ts'
  );
  const conversationSpot = read(
    'samples/SupportChat.Ts/Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation-spot.ts'
  );

  assert.match(contracts, /type OpenConversationApiRes = \{ conversationId: string; status: ConversationStatus \};/);
  assert.match(contracts, /type AllocateConversationRes = \{ conversationId: string; status: ConversationStatus \};/);
  assert.doesNotMatch(allocator, /\.joinSpot\(/);
  assert.match(entry, /actor\.context\.joinSpot\(\s*opened\.conversationId/);
  assert.match(entry, /state:\s*joined\.reply\.state/);
  assert.doesNotMatch(entry, /status:\s*ConversationStatuses\.WaitingForAgent/);
  assert.doesNotMatch(entry, /ZLINK_SPOT_HANDLE_RESOLVER|ZLINK_SPOT_OUTBOUND|requestToSpot/);
  assert.match(conversationSpot, /this\.assignments\.assignNextAgent\(\)/);
});
