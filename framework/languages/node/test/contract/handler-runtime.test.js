const assert = require('node:assert/strict');
const test = require('node:test');

const {
  ZLinkHandlerGroup,
  ZLinkRequest,
  ZLinkSend,
  exposeZLinkHandlers,
  invokeZLinkHandlerFilters,
  scanZLinkHandlerTypes
} = require('../../packages/framework/dist/internal');

test('handler scan does not expose handlers unless policy selects them', () => {
  class PublicHandler {}
  class PrivateHandler {}

  ZLinkHandlerGroup('api')(PublicHandler);
  ZLinkRequest('GetProfile')(PublicHandler.prototype, 'handle', descriptor());
  ZLinkSend('InternalCommand')(PrivateHandler.prototype, 'handle', descriptor());

  assert.equal(scanZLinkHandlerTypes([PublicHandler, PrivateHandler]).length, 2);
  assert.deepEqual(
    exposeZLinkHandlers([PublicHandler, PrivateHandler], { handlerGroups: ['api'] }).map((descriptor) => descriptor.handlerType),
    [PublicHandler]
  );
  assert.deepEqual(exposeZLinkHandlers([PublicHandler, PrivateHandler], { handlerGroups: [] }), []);
  assert.deepEqual(
    exposeZLinkHandlers([PublicHandler, PrivateHandler], { explicitHandlers: [PrivateHandler] }).map((descriptor) => descriptor.handlerType),
    [PrivateHandler]
  );
});

test('handler filters run before and after in registration order', async () => {
  const events = [];
  const invocation = {
    ownerKind: 'channel',
    messageContext: {
      channelName: 'api',
      packetName: 'Ping',
      metadata: new Map()
    }
  };
  const filters = [
    {
      async invoke(actual, next) {
        assert.equal(actual, invocation);
        events.push('a:before');
        const result = await next();
        events.push('a:after');
        return result;
      }
    },
    {
      async invoke(actual, next) {
        assert.equal(actual, invocation);
        events.push('b:before');
        const result = await next();
        events.push('b:after');
        return result;
      }
    }
  ];

  const result = await invokeZLinkHandlerFilters(filters, invocation, async () => {
    events.push('handler');
    return 42;
  });

  assert.equal(result, 42);
  assert.deepEqual(events, ['a:before', 'b:before', 'handler', 'b:after', 'a:after']);
});

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value() {},
    writable: true
  };
}
