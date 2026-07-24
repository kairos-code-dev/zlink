import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  ZLinkUserSpotExecutionMode
} from '../../packages/framework/src/contracts/Configuration/ObjectRoles';
import { ZLinkConfigurationException } from '../../packages/framework/src/runtime/configuration';
import { ZLinkSpotActivation } from '../../packages/framework/src/runtime/spots/spot-activation-state';
import { ZLinkSpotSerialExecutor } from '../../packages/framework/src/runtime/spots/spot-serial-executor';
import { ZLinkSpotTimerRegistry } from '../../packages/framework/src/runtime/spots/spot-timer';
import { DefaultZLinkWorkerCall } from '../../packages/framework/src/runtime/workers';

interface Deferred<T> {
  readonly promise: Promise<T>;
  resolve(value: T): void;
}

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((complete) => {
    resolve = complete;
  });
  return { promise, resolve };
}

function activation(
  serial: ZLinkSpotSerialExecutor,
  executionMode: ZLinkUserSpotExecutionMode
): ZLinkSpotActivation {
  return new ZLinkSpotActivation({
    meshName: 'mesh',
    spotId: 'spot-1' as never,
    spotType: class TestSpot {} as never,
    spot: {} as never,
    serial,
    executionMode,
    timers: {} as never,
    actorHandlers: {} as never,
    handlers: {} as never
  });
}

test('SpotWide Yield releases the Spot gate but retains the Actor claim', async () => {
  const serial = new ZLinkSpotSerialExecutor(undefined, 'user', true);
  const state = activation(serial, ZLinkUserSpotExecutionMode.SpotWide);
  const response = deferred<string>();
  const events: string[] = [];

  const firstActorJob = state.executeActor('actor-a', actorSerial =>
    actorSerial.execute(async () => {
      events.push('actor-a:start');
      const value = await actorSerial.yieldPromise(response.promise);
      events.push(`actor-a:${value}`);
    })
  );
  await Promise.resolve();

  const secondActorJob = state.executeActor('actor-a', actorSerial =>
    actorSerial.execute(() => {
      events.push('actor-a:next');
    })
  );
  const otherActorJob = state.executeActor('actor-b', actorSerial =>
    actorSerial.execute(() => {
      events.push('actor-b');
    })
  );
  const spotJob = serial.execute(() => {
    events.push('spot');
  });

  await Promise.all([otherActorJob, spotJob]);
  assert.deepEqual([...events].sort(), ['actor-a:start', 'actor-b', 'spot']);

  response.resolve('resume');
  await Promise.all([firstActorJob, secondActorJob]);
  assert.deepEqual(events.slice(-2), ['actor-a:resume', 'actor-a:next']);
});

test('PerActor keeps Actor continuations ordered while Actor and Spot lanes run independently', async () => {
  const spotSerial = new ZLinkSpotSerialExecutor(undefined, 'user', false);
  const state = activation(spotSerial, ZLinkUserSpotExecutionMode.PerActor);
  const response = deferred<string>();
  const events: string[] = [];

  const firstActorJob = state.executeActor('actor-a', actorSerial =>
    actorSerial.execute(async () => {
      events.push('actor-a:start');
      const value = await response.promise;
      events.push(`actor-a:${value}`);
    })
  );
  await Promise.resolve();

  const secondActorJob = state.executeActor('actor-a', actorSerial =>
    actorSerial.execute(() => {
      events.push('actor-a:next');
    })
  );
  const otherActorJob = state.executeActor('actor-b', actorSerial =>
    actorSerial.execute(() => {
      events.push('actor-b');
    })
  );
  const spotJob = spotSerial.execute(() => {
    events.push('spot');
  });

  await Promise.all([otherActorJob, spotJob]);
  assert.deepEqual([...events].sort(), ['actor-a:start', 'actor-b', 'spot']);

  response.resolve('continued');
  await Promise.all([firstActorJob, secondActorJob]);
  assert.deepEqual(events.slice(-2), ['actor-a:continued', 'actor-a:next']);
});

test('Yield rejects outside an allowed gate before worker admission', async () => {
  let scheduled = 0;
  const outsideCall = new DefaultZLinkWorkerCall(
    new ZLinkSpotSerialExecutor(),
    async () => {
      scheduled += 1;
      return 'outside';
    }
  );
  assert.throws(
    () => outsideCall.yield(),
    ZLinkConfigurationException
  );
  assert.equal(scheduled, 0);

  const perActorSerial = new ZLinkSpotSerialExecutor(undefined, 'user', false);
  await perActorSerial.execute(() => {
    const call = new DefaultZLinkWorkerCall(
      perActorSerial,
      async () => {
        scheduled += 1;
        return 'inside';
      }
    );
    assert.throws(
      () => call.yield(),
      ZLinkConfigurationException
    );
    assert.equal(scheduled, 0);
  });
});

test('PerActor timer registrations select an independent lane per timer name', async () => {
  const spotSerial = new ZLinkSpotSerialExecutor(undefined, 'user', false);
  const timerSerials = new Map<string, ZLinkSpotSerialExecutor>();
  const registry = new ZLinkSpotTimerRegistry(
    undefined,
    () => false,
    (name) => {
      let serial = timerSerials.get(name);
      if (serial === undefined) {
        serial = new ZLinkSpotSerialExecutor(undefined, 'user', false);
        timerSerials.set(name, serial);
      }
      return serial;
    }
  );
  class TimerHandler {
    handle(): void {}
  }

  await registry.add(
    'heartbeat',
    10_000,
    undefined,
    TimerHandler as never,
    spotSerial,
    {} as never
  );
  await registry.add(
    'expiry',
    10_000,
    undefined,
    TimerHandler as never,
    spotSerial,
    {} as never
  );

  assert.notEqual(timerSerials.get('heartbeat'), timerSerials.get('expiry'));
  assert.notEqual(timerSerials.get('heartbeat'), spotSerial);
  assert.notEqual(timerSerials.get('expiry'), spotSerial);
  await registry.dispose();
});
