const assert = require('node:assert/strict');
const test = require('node:test');

const {
  createAbortError,
  throwIfAborted
} = require('../../packages/framework/dist/runtime/abort');

test('framework abort policy uses the stable plain Error contract', () => {
  const error = createAbortError();

  assert.equal(error.constructor, Error);
  assert.equal(error.message, 'The operation was aborted.');
  assert.doesNotThrow(() => throwIfAborted(undefined));
  assert.doesNotThrow(() => throwIfAborted(new AbortController().signal));

  const controller = new AbortController();
  controller.abort();
  assert.throws(
    () => throwIfAborted(controller.signal),
    (cause) => cause?.constructor === Error && cause.message === 'The operation was aborted.'
  );
});
