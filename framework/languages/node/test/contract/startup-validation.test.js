const assert = require('node:assert/strict');
const test = require('node:test');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const framework = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');

test('Node registration rejects server and subscriber capabilities without matching handlers', () => {
  assert.throws(
    () => framework.createFrameworkRegistration(framework.createFrameworkOptions((builder) => {
      builder.addClientServerChannel('api').enableServer('tcp://127.0.0.1:0');
    })),
    /server must register at least one request or send handler/
  );

  assert.throws(
    () => framework.createFrameworkRegistration(framework.createFrameworkOptions((builder) => {
      builder.addFanoutChannel('events').enableSubscriber('tcp://127.0.0.1:1');
    })),
    /subscriber must register at least one publish handler/
  );
});

test('Node module registration rejects server and subscriber capabilities without matching handlers', () => {
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addClientServerChannel('api')
        .enableServer('tcp://127.0.0.1:0')
      .build()),
    /server must register at least one request or send handler/
  );

  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber('tcp://127.0.0.1:1')
      .build()),
    /subscriber must register at least one publish handler/
  );
});

test('Node registration rejects incomplete SpotNode capabilities before startup', async () => {
  await assertNestStartupRejects(
    nestjs.zlinkFramework()
      .addSpotMesh('empty')
      .build(),
    /must enable router or pubSub capability/
  );

  await assertNestStartupRejects(
    nestjs.zlinkFramework()
      .addSpotMesh('missing-bind')
        .enableRouter(undefined)
      .build(),
    /router must define a bind endpoint/
  );

  class ActorFactory {}
  await assertNestStartupRejects(
    nestjs.zlinkFramework()
      .addSpotMesh('actors')
        .enablePubSub('tcp://127.0.0.1:0')
        .actorFactory('player', ActorFactory)
      .build(),
    /must enable router capability when actor factories are registered/
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
