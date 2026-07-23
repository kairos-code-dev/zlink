const assert = require('node:assert/strict');
const test = require('node:test');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const framework = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');

test('Node registration rejects subscriber capability without matching handlers', () => {
  assert.throws(
    () => framework.createFrameworkRegistration(framework.createFrameworkOptions((builder) => {
      builder.addFanoutChannel('events').enableSubscriber('tcp://127.0.0.1:1');
    })),
    /subscriber must register at least one publish handler/
  );
});

test('Node registration rejects mixed automatic and manual fanout subscriber sources', () => {
  for (const automaticFirst of [true, false]) {
    assert.throws(
      () => framework.createFrameworkOptions((builder) => {
        const fanout = builder.addFanoutChannel(`events-${automaticFirst}`);
        if (automaticFirst) {
          fanout.enableSubscriber();
          fanout.enableSubscriber('tcp://127.0.0.1:7001');
        } else {
          fanout.enableSubscriber('tcp://127.0.0.1:7001');
          fanout.enableSubscriber();
        }
      }),
      /cannot combine automatic and manual subscriber sources/
    );
  }
});

test('Node module registration rejects mixed automatic and manual fanout subscriber sources', () => {
  assert.throws(
    () => nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber()
        .enableSubscriber('tcp://127.0.0.1:7001'),
    /cannot combine automatic and manual subscriber sources/
  );
});

test('Node module registration rejects subscriber capability without matching handlers', () => {
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber('tcp://127.0.0.1:1')
      .build()),
    /subscriber must register at least one publish handler/
  );
});

test('Node registration rejects incomplete MeshNode capabilities before startup', async () => {
  await assertNestStartupRejects(
    nestjs.zlinkFramework()
      .addRouteMesh('empty')
      .build(),
    /must enable router or pubSub capability/
  );

  await assertNestStartupRejects(
    nestjs.zlinkFramework()
      .addRouteMesh('missing-bind')
        .listen(undefined)
      .build(),
    /router must define a bind endpoint/
  );

});

test('Node registration requires durable Actor transfer authority when transfer adapters are enabled', () => {
  class Player {}
  class PlayerTransferAdapter {}
  const base = {
    actorTransferAdapters: new Map([[Player, PlayerTransferAdapter]])
  };

  assert.throws(
    () => framework.createFrameworkRegistration(base),
    /Actor transfer adapters require a durable location store with Actor transfer authority/
  );
  assert.throws(
    () => framework.createFrameworkRegistration({
      ...base,
      locations: { useInMemoryStores: true }
    }),
    /Actor transfer adapters require a durable location store with Actor transfer authority/
  );
  assert.throws(
    () => framework.createFrameworkRegistration({
      ...base,
      locations: { storeInstance: { updateActor() {} } }
    }),
    /Actor transfer adapters require a durable location store with Actor transfer authority/
  );
});

test('Node registration rejects invalid Spot timer options before startup', () => {
  class GameSpot {}
  class EntrySpot {}
  class TimerHandler {}
  const registrationWithTimer = (timerKind, timer) => ({
    spotNodes: {
      game: {
        router: { bind: 'tcp://127.0.0.1:0' },
        [timerKind]: [timer]
      }
    }
  });

  assert.throws(
    () => framework.createFrameworkRegistration(registrationWithTimer('spotTimerHandlers', {
      spotType: GameSpot,
      handlerType: TimerHandler,
      name: 'tick',
      periodMs: 0
    })),
    /period must be greater than zero/
  );
  assert.throws(
    () => framework.createFrameworkRegistration(registrationWithTimer('entrySpotTimerHandlers', {
      entrySpotType: EntrySpot,
      handlerType: TimerHandler,
      name: '',
      periodMs: 100
    })),
    /name must not be empty/
  );
  assert.throws(
    () => framework.createFrameworkRegistration(registrationWithTimer('spotTimerHandlers', {
      spotType: GameSpot,
      handlerType: TimerHandler,
      name: 'tick',
      periodMs: 100,
      options: { overrunPolicy: 'unsupported' }
    })),
    /overrun policy is not supported/
  );
  assert.throws(
    () => framework.createFrameworkRegistration(registrationWithTimer('spotTimerHandlers', {
      spotType: GameSpot,
      handlerType: TimerHandler,
      name: 'tick',
      periodMs: 100,
      options: {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.CatchUpBounded,
        maxCatchUpTicks: 0
      }
    })),
    /MaxCatchUpTicks must be greater than zero/
  );
});

test('Node one-way send timeout accepts only integer milliseconds in the public range', () => {
  const invalid = [0, -1, 1.5, Number.NaN, Number.POSITIVE_INFINITY, 2_147_483_648];
  for (const sendTimeoutMs of invalid) {
    assert.throws(
      () => framework.createFrameworkRegistration({
        channels: {
          api: {
            client: {
              manualConnections: ['tcp://127.0.0.1:7101'],
              sendTimeoutMs
            }
          }
        }
      }),
      /between 1 and 2147483647 milliseconds/
    );
    assert.throws(
      () => framework.createFrameworkRegistration({
        spotNodes: {
          play: {
            router: { bind: 'tcp://127.0.0.1:7102' },
            publisherConfig: { sendTimeoutMs }
          }
        }
      }),
      /between 1 and 2147483647 milliseconds/
    );
  }

  framework.createFrameworkRegistration({
    channels: {
      api: {
        client: {
          manualConnections: ['tcp://127.0.0.1:7101'],
          sendTimeoutMs: 2_147_483_647
        }
      }
    }
  });
});

test('Node live socket send timeout setter applies the same public range', () => {
  const socket = {
    peerWeight: 100,
    sendHighWaterMark: 1000,
    receiveHighWaterMark: 1000,
    sendTimeoutMs: 1000,
    maxMessageSize: 1024
  };
  const options = new framework.DefaultZLinkChannelRuntimeOptions(() => ({
    clientServerServerSocket() { return socket; },
    routeMeshSocket() { return socket; }
  }));
  const config = options.serverChannel('api');

  for (const sendTimeoutMs of [0, -1, 1.5, Number.NaN, Number.POSITIVE_INFINITY, 2_147_483_648]) {
    assert.throws(
      () => { config.sendTimeoutMs = sendTimeoutMs; },
      /between 1 and 2147483647 milliseconds/
    );
  }
  config.sendTimeoutMs = 2_147_483_647;
  assert.equal(socket.sendTimeoutMs, 2_147_483_647);
});

test('ClientServer role builders map only the selected role into the channel runtime', () => {
  class NoticeHandler {}
  class QueryHandler {}

  const options = framework.createFrameworkOptions((builder) => {
    builder.addClientServerChannel('orders.client')
      .client()
      .connect('tcp://127.0.0.1:9401');
    builder.addClientServerChannel('orders.server')
      .server()
      .setBindHost('0.0.0.0')
      .setAdvertiseHost('orders.internal')
      .listen(9401)
      .setWeight(75)
      .addSendHandler(NoticeHandler)
      .addRequestHandler(QueryHandler);
  });
  const registration = framework.createFrameworkRegistration(options);
  const client = registration.channels.get('orders.client');
  const server = registration.channels.get('orders.server');

  assert.deepEqual(client.client.manualConnections, ['tcp://127.0.0.1:9401']);
  assert.equal(client.server, undefined);
  assert.equal(server.client, undefined);
  assert.equal(server.server.bind, 'tcp://0.0.0.0:9401');
  assert.equal(server.server.advertiseHost, 'orders.internal');
  assert.equal(server.server.weight, 75);
  assert.deepEqual(server.sendHandlers.map((handler) => handler.handlerType), [NoticeHandler]);
  assert.deepEqual(server.requestHandlers.map((handler) => handler.handlerType), [QueryHandler]);
  assert.deepEqual([...registration.channelClients], ['orders.client']);
});

test('ClientServer registration rejects duplicate topology names and repeated role selection', () => {
  assert.throws(() => framework.createFrameworkOptions((builder) => {
    builder.addClientServerChannel('orders');
    builder.addFanoutChannel('orders');
  }), /Duplicate channel 'orders'/);

  assert.throws(() => framework.createFrameworkOptions((builder) => {
    const role = builder.addClientServerChannel('orders');
    role.client();
    role.server();
  }), /role is already selected/);
});

async function assertNestStartupRejects(options, pattern) {
  class InvalidConfigurationModule {}
  Module({ imports: [nestjs.ZLinkModule.forRoot(options)] })(InvalidConfigurationModule);
  await assert.rejects(
    () => NestFactory.createApplicationContext(InvalidConfigurationModule, {
      logger: false,
      abortOnError: false
    }),
    pattern
  );
}
