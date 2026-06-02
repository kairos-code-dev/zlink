const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist');

test('backend adapter factory exposes the five backend adapters', () => {
  const factory = new framework.ZLinkNodeBackendAdapterFactory();

  assert.equal(typeof factory.createChannelAdapter, 'function');
  assert.equal(typeof factory.createSpotAdapter, 'function');
  assert.equal(typeof factory.createStreamAdapter, 'function');
  assert.equal(typeof factory.createRegistryAdapter, 'function');
  assert.equal(typeof factory.createMonitoringAdapter, 'function');
});

test('backend adapter creates context and core socket wrappers through public binding API', async () => {
  const factory = new framework.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const context = channel.createContext();
  const disposables = [];

  try {
    const dealer = channel.createDealerSocket(context);
    const router = channel.createRouterSocket(context);
    const publisher = channel.createPublisherSocket(context);
    const subscriber = channel.createSubscriberSocket(context);
    const stream = factory.createStreamAdapter().createStreamSocket(context);
    const registry = factory.createRegistryAdapter().createRegistry(context);
    const registryQueryClient = factory.createRegistryAdapter().createRegistryQueryClient(context);

    disposables.push(dealer, router, publisher, subscriber, stream, registry, registryQueryClient);

    assert.equal(typeof dealer.dispose, 'function');
    assert.equal(typeof router.dispose, 'function');
    assert.equal(typeof publisher.dispose, 'function');
    assert.equal(typeof subscriber.dispose, 'function');
    assert.equal(typeof stream.dispose, 'function');
    assert.equal(typeof registry.bind, 'function');
    assert.equal(typeof registry.dispose, 'function');
    assert.equal(typeof registryQueryClient.connect, 'function');
    assert.equal(typeof registryQueryClient.dispose, 'function');
  } finally {
    for (const disposable of disposables.reverse()) {
      await disposable.dispose();
    }
    await context.dispose();
  }
});
