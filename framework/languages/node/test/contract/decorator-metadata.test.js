const assert = require('node:assert/strict');
const test = require('node:test');

const {
  ZLinkHandlerGroup,
  ZLinkPacket,
  ZLinkRequest,
  ZLinkSend,
  readZLinkDecoratorMetadata
} = require('../../packages/framework/dist');

test('handler decorators accumulate metadata without requiring reflect-metadata', () => {
  class Handler {}

  ZLinkHandlerGroup('api')(Handler);
  ZLinkPacket('profile.changed')(Handler);
  ZLinkRequest('getProfile')(Handler.prototype, 'getProfile', descriptor());
  ZLinkSend('updateProfile')(Handler.prototype, 'updateProfile', descriptor());

  assert.deepEqual(readZLinkDecoratorMetadata(Handler), [
    { kind: 'handlerGroup', groupName: 'api' },
    { kind: 'packet', packetName: 'profile.changed' },
    { kind: 'request', packetName: 'getProfile', methodName: 'getProfile' },
    { kind: 'send', packetName: 'updateProfile', methodName: 'updateProfile' }
  ]);
});

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value() {},
    writable: true
  };
}
