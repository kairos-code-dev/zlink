import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  ServiceMaintenanceRuntime,
  classifyRelocationRecovery
} from '../../packages/framework/src/runtime/foundation/service-maintenance-runtime';

test('Retire preflight precedes publication and ready units use bounded permits', async () => {
  const events: string[] = [];
  let active = 0;
  let peak = 0;
  const runtime = new ServiceMaintenanceRuntime({
    maxOutbound: 2,
    maxInFlightBytes: 20,
    preflight: async () => {
      events.push('preflight');
      return true;
    },
    publishState: state => events.push(state),
    forceStop: () => {
      events.push('force');
    }
  });
  for (let index = 0; index < 4; index++) {
    runtime.enqueue({
      id: `unit-${index}`,
      encodedUpperBound: 10,
      ready: () => true,
      relocate: async () => {
        active++;
        peak = Math.max(peak, active);
        await new Promise(resolve => setImmediate(resolve));
        active--;
      }
    });
  }
  const terminal = await runtime.start('retire', 2_000);
  assert.equal(terminal.state, 'completed');
  assert.equal(peak, 2);
  assert.deepEqual(events.slice(0, 3), ['preflight', 'retiring', 'draining']);
});

test('first maintenance intent wins and blocked preflight keeps admission uncommitted', async () => {
  const runtime = new ServiceMaintenanceRuntime({
    preflight: async () => false,
    publishState: () => assert.fail('blocked preflight must not publish host state'),
    forceStop: () => assert.fail('preflight failure must not force stop')
  });
  const first = runtime.start('retire', 100);
  const second = runtime.start('shutdown', 100);
  assert.equal(first, second);
  const terminal = await first;
  assert.equal(terminal.kind, 'retire');
  assert.equal(terminal.state, 'blocked');
});

test('deadline after publication forces bounded terminal shutdown and observers see it', async () => {
  const states: string[] = [];
  let forced = 0;
  const runtime = new ServiceMaintenanceRuntime({
    preflight: async () => true,
    publishState: () => {},
    forceStop: () => {
      forced++;
    }
  });
  runtime.observe(snapshot => states.push(snapshot.state));
  runtime.enqueue({
    id: 'slow',
    encodedUpperBound: 1,
    ready: () => true,
    relocate: signal => new Promise((_, reject) => {
      signal.addEventListener('abort', () => reject(signal.reason), { once: true });
    })
  });
  const terminal = await runtime.start('retire', 5);
  assert.equal(terminal.state, 'forceStopped');
  assert.equal(forced, 1);
  assert.equal(states.includes('forceStopped'), true);
});

test('recovery never rolls a published missing or corrupt root back to source', () => {
  assert.equal(classifyRelocationRecovery(false, true, true, true), 'orphan');
  assert.equal(classifyRelocationRecovery(true, false, true, true), 'relocationDataLost');
  assert.equal(classifyRelocationRecovery(true, true, false, true), 'relocationDataLost');
  assert.equal(classifyRelocationRecovery(true, true, true, false), 'relocationDataLost');
  assert.equal(classifyRelocationRecovery(true, true, true, true), 'resume');
});
