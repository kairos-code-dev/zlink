const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');
const framework = require('../../packages/framework/dist');

test('ZLinkSpotManager creates lists finds and removes spots with lifecycle order', async () => {
  const events = [];
  class StageSpot {
    configure() {
      events.push('configure');
    }
    async onCreate(parts) {
      events.push(`onCreate:${parts[0].data().toString()}`);
    }
    async onInitialize() {
      events.push('onInitialize');
    }
    async onClosing() {
      events.push('onClosing');
    }
  }

  const createPart = zlink.Message.from('open');
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  try {
    const created = await manager.create(StageSpot, [createPart]);
    assert.equal(created.created, true);
    assert.equal(typeof created.spotRid, 'string');
    assert.deepEqual(events, ['configure', 'onCreate:open', 'onInitialize']);
    assert.deepEqual(await manager.find(created.spotRid), { spotRid: created.spotRid });
    assert.deepEqual(await manager.list(), [{ spotRid: created.spotRid }]);
    assert.equal(await manager.remove(created.spotRid), true);
    assert.equal(await manager.remove(created.spotRid), false);
    assert.equal(await manager.find(created.spotRid), null);
    assert.deepEqual(events, ['configure', 'onCreate:open', 'onInitialize', 'onClosing']);
  } finally {
    createPart.close();
  }
});

test('ZLinkSpotManager passes dotnet-shaped context into spot constructor', async () => {
  let capturedContext;
  class StageSpot {
    constructor(context) {
      capturedContext = context;
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    nodeRid: 'node-a'
  });
  const created = await manager.getOrCreate(StageSpot, 'stage-a');

  assert.equal(capturedContext.spotRid, 'stage-a');
  assert.equal(capturedContext.routingId, 'stage-a');
  assert.equal(capturedContext.nodeRid, 'node-a');
  assert.equal(typeof capturedContext.addTimer, 'function');
  assert.equal(typeof capturedContext.handlers.addPacket, 'function');
  assert.equal(created.created, true);
});

test('spot handler registry records packet and subscribe registrations from configure', async () => {
  class PacketHandler {}
  class SubscribeHandler {}
  let registry;
  class StageSpot {
    configure() {
      this.context.handlers.addPacket(PacketHandler, 'stage.packet');
      this.context.handlers.addSubscribe(SubscribeHandler, 'stage.updated');
      registry = this.context.handlers;
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  await manager.create(StageSpot);

  assert.deepEqual(registry.snapshot(), [
    { kind: 'packet', handlerType: PacketHandler, packetName: 'stage.packet' },
    { kind: 'subscribe', handlerType: SubscribeHandler, topic: 'stage.updated' }
  ]);
});

test('ZLinkSpotManager getOrCreate is keyed by spot type and spotRid', async () => {
  class StageSpot {}
  class OtherSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot, OtherSpot] });

  assert.deepEqual(await manager.getOrCreate(StageSpot, 'stage-1'), { spotRid: 'stage-1', created: true });
  assert.deepEqual(await manager.getOrCreate(StageSpot, 'stage-1'), { spotRid: 'stage-1', created: false });
  await assert.rejects(
    () => manager.getOrCreate(OtherSpot, 'stage-1'),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkSpotManager rejects unregistered spot factories', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [] });

  await assert.rejects(
    () => manager.create(StageSpot),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkSpotSerialExecutor runs spot work in submission order', async () => {
  const executor = new framework.ZLinkSpotSerialExecutor();
  const events = [];

  const first = executor.execute(async () => {
    events.push('first:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('first:end');
  });
  const second = executor.execute(async () => {
    events.push('second');
  });

  await Promise.all([first, second]);
  assert.deepEqual(events, ['first:start', 'first:end', 'second']);
});

test('spot outbound requestToChannel completion runs on the spot serial executor', async () => {
  const events = [];
  class StageSpot {}
  const channelClient = {
    requestToChannel(channelName, request) {
      return {
        packetName() { return this; },
        timeout() { return this; },
        async submit() {
          events.push(`request:${channelName}:${request}`);
          return 'reply';
        }
      };
    },
    sendToChannel() {
      throw new Error('not used');
    },
    request() {
      throw new Error('not used');
    },
    send() {
      throw new Error('not used');
    }
  };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    channelClient
  });
  const created = await manager.create(StageSpot);
  let outbound;
  await manager.executeOnSpot(created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  const first = manager.executeOnSpot(created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });
  const reply = await outbound.requestToChannel('api', 'ping').submit();
  await first;

  assert.equal(reply, 'reply');
  assert.deepEqual(events, ['spot:start', 'spot:end', 'request:api:ping']);
});

test('spot outbound routed send and request resolve remote address inside serial executor', async () => {
  const events = [];
  class StageSpot {}
  const remoteAddress = {
    routerChannelId: 'play.route',
    targetNodeRid: 'node-b',
    spotRid: 'stage-b',
    spotKind: framework.ZLinkSpotKind.User
  };
  const remoteAddressResolver = {
    async resolve(spotRid) {
      events.push(`resolve:${spotRid}`);
      return remoteAddress;
    }
  };
  const routedTransport = {
    async sendToSpot(address, message, options) {
      events.push(`send:${address.targetNodeRid}:${address.spotRid}:${options.packetName}:${message}`);
    },
    async requestToSpot(address, request, options) {
      events.push(`request:${address.routerChannelId}:${address.spotRid}:${options.timeoutMs}:${request}`);
      return 'routed-reply';
    }
  };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    remoteAddressResolver,
    routedTransport
  });
  const created = await manager.create(StageSpot);
  let outbound;
  await manager.executeOnSpot(created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  const first = manager.executeOnSpot(created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });
  const send = outbound.sendToSpot('stage-b', 'notice').packetName('Notice').submit();
  const reply = await outbound.requestToSpot('stage-b', 'ping').packetName('Ping').timeout(250).submit();
  await send;
  await first;

  assert.equal(reply, 'routed-reply');
  assert.deepEqual(events, [
    'spot:start',
    'spot:end',
    'resolve:stage-b',
    'send:node-b:stage-b:Notice:notice',
    'resolve:stage-b',
    'request:play.route:stage-b:250:ping'
  ]);
});

test('spot outbound routed calls require resolver and runtime transport', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot);
  let outbound;
  await manager.executeOnSpot(created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  assert.throws(
    () => outbound.sendToSpot('stage-b', 'notice'),
    framework.ZLinkConfigurationException
  );

  const managerWithoutTransport = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    remoteAddressResolver: { async resolve() { throw new Error('not used'); } }
  });
  const second = await managerWithoutTransport.create(StageSpot);
  await managerWithoutTransport.executeOnSpot(second.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  assert.throws(
    () => outbound.requestToSpot('stage-b', 'ping'),
    framework.ZLinkConfigurationException
  );
});

test('spot timer dispatches handler on the spot serial executor with dotnet tick metadata', async () => {
  const events = [];
  let firstTick;
  const tickReceived = new Promise((resolve) => {
    firstTick = resolve;
  });
  class HeartbeatHandler {
    async handle(spot, tick) {
      events.push(`tick:${tick.deliveryIndex}:${spot.context.spotRid}`);
      firstTick(tick);
    }
  }
  class StageSpot {
    async onInitialize() {
      this.timer = await this.context.addTimer(
        'heartbeat',
        1,
        HeartbeatHandler,
        { overrunPolicy: framework.ZLinkTimerOverrunPolicy.DelayNextTick }
      );
    }
    async onClosing() {
      events.push(`closing:${this.timer.isDisposed}`);
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot);
  const blockingTurn = manager.executeOnSpot(created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });

  const tick = await tickReceived;
  await blockingTurn;
  await manager.remove(created.spotRid);

  assert.equal(tick.name, 'heartbeat');
  assert.equal(tick.deliveryIndex, 1n);
  assert.equal(tick.scheduledIndex, 1n);
  assert.equal(tick.periodMs, 1);
  assert.equal(tick.skippedTicks, 0n);
  assert.equal(tick.scheduledAt instanceof Date, true);
  assert.equal(tick.startedAt instanceof Date, true);
  assert.deepEqual(events, ['spot:start', 'spot:end', 'tick:1:spot-1', 'closing:false']);
});

test('spot timer rejects invalid options', async () => {
  class Handler {
    async handle() {}
  }
  class StageSpot {}

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot);

  await assert.rejects(
    () => manager.executeOnSpot(created.spotRid, (spot) => spot.context.addTimer('', 1, Handler)),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => manager.executeOnSpot(created.spotRid, (spot) => spot.context.addTimer('bad-period', 0, Handler)),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => manager.executeOnSpot(created.spotRid, (spot) =>
      spot.context.addTimer('bad-policy', 1, Handler, { overrunPolicy: 'unsupported' })
    ),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => manager.executeOnSpot(created.spotRid, (spot) =>
      spot.context.addTimer('bad-catchup', 1, Handler, {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.CatchUpBounded,
        maxCatchUpTicks: 0
      })
    ),
    framework.ZLinkConfigurationException
  );
});

test('spot managed timer overrun policies follow dotnet skip catch-up and fixed-delay semantics', async () => {
  await withFakeTimerClock(async (clock) => {
    const skipLateTicks = [];
    const skipLateTimer = new framework.ZLinkManagedTimer(
      'skip-late',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.SkipLateTicks,
        maxCatchUpTicks: 1,
        stopOnUnhandledException: false
      },
      async (tick) => {
        skipLateTicks.push(tick);
        if (skipLateTicks.length === 1) {
          clock.advanceBy(35);
        }
      }
    );

    await clock.runNext();
    await clock.runNext();
    await skipLateTimer.cancel();

    assert.deepEqual(skipLateTicks.map((tick) => tick.scheduledIndex), [1n, 4n]);
    assert.equal(skipLateTicks[1].skippedTicks, 2n);
  });

  await withFakeTimerClock(async (clock) => {
    const catchUpTicks = [];
    const catchUpTimer = new framework.ZLinkManagedTimer(
      'catch-up',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.CatchUpBounded,
        maxCatchUpTicks: 2,
        stopOnUnhandledException: false
      },
      async (tick) => {
        catchUpTicks.push(tick);
        if (catchUpTicks.length === 1) {
          clock.advanceBy(35);
        }
      }
    );

    await clock.runNext();
    await clock.runNext();
    await clock.runNext();
    await catchUpTimer.cancel();

    assert.deepEqual(catchUpTicks.map((tick) => tick.scheduledIndex), [1n, 3n, 4n]);
    assert.equal(catchUpTicks[1].skippedTicks, 1n);
    assert.equal(catchUpTicks[2].skippedTicks, 0n);
  });

  await withFakeTimerClock(async (clock) => {
    const delayNextTicks = [];
    const delayNextTimer = new framework.ZLinkManagedTimer(
      'delay-next',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.DelayNextTick,
        maxCatchUpTicks: 1,
        stopOnUnhandledException: false
      },
      async (tick) => {
        delayNextTicks.push(tick);
        if (delayNextTicks.length === 1) {
          clock.advanceBy(35);
        }
      }
    );

    await clock.runNext();
    assert.deepEqual(clock.pendingDelays(), [10]);
    await clock.runNext();
    await delayNextTimer.cancel();

    assert.deepEqual(delayNextTicks.map((tick) => tick.scheduledIndex), [1n, 2n]);
    assert.equal(delayNextTicks[1].skippedTicks, 0n);
  });
});

test('spot managed timer stopOnUnhandledException stops after handler failure', async () => {
  await withFakeTimerClock(async (clock) => {
    let attempts = 0;
    const timer = new framework.ZLinkManagedTimer(
      'failing',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.SkipLateTicks,
        maxCatchUpTicks: 1,
        stopOnUnhandledException: true
      },
      async () => {
        attempts += 1;
        throw new Error('timer failed');
      }
    );

    await clock.runNext();

    assert.equal(attempts, 1);
    assert.equal(timer.isDisposed, true);
    assert.deepEqual(clock.pendingDelays(), []);
  });
});

async function withFakeTimerClock(run) {
  const originalNow = Date.now;
  const originalSetTimeout = global.setTimeout;
  const originalClearTimeout = global.clearTimeout;
  let now = 0;
  let nextId = 1;
  const timers = [];

  Date.now = () => now;
  global.setTimeout = (callback, delay) => {
    const timer = {
      id: nextId++,
      callback,
      delay: Number(delay) || 0,
      cleared: false
    };
    timers.push(timer);
    return timer;
  };
  global.clearTimeout = (timer) => {
    if (timer && typeof timer === 'object') {
      timer.cleared = true;
      return;
    }
    const found = timers.find((entry) => entry.id === timer);
    if (found !== undefined) {
      found.cleared = true;
    }
  };

  const clock = {
    advanceBy(ms) {
      now += ms;
    },
    async runNext() {
      const timer = timers.shift();
      assert.ok(timer, 'expected a scheduled timer callback');
      if (timer.cleared) {
        return;
      }
      now += timer.delay;
      timer.callback();
      await Promise.resolve();
      await Promise.resolve();
    },
    pendingDelays() {
      return timers
        .filter((timer) => !timer.cleared)
        .map((timer) => timer.delay);
    }
  };

  try {
    await run(clock);
  } finally {
    Date.now = originalNow;
    global.setTimeout = originalSetTimeout;
    global.clearTimeout = originalClearTimeout;
  }
}
