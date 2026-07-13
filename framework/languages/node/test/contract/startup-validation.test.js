const assert = require('node:assert/strict');
const test = require('node:test');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const nestjs = require('../../packages/nestjs/dist');

test('Node startup rejects server and subscriber capabilities without matching handlers', async () => {
  class ServerWithoutHandlersModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addClientServerChannel('api')
        .enableServer('tcp://127.0.0.1:0')
      .build())]
  })(ServerWithoutHandlersModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(ServerWithoutHandlersModule, {
      logger: false,
      abortOnError: false
    }),
    /server must register at least one request or send handler/
  );

  class SubscriberWithoutHandlersModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber('tcp://127.0.0.1:1')
      .build())]
  })(SubscriberWithoutHandlersModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(SubscriberWithoutHandlersModule, {
      logger: false,
      abortOnError: false
    }),
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
