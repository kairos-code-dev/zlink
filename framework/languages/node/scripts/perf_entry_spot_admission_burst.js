#!/usr/bin/env node
// Admission-burst benchmark for Entry Spot actor packet dispatch.
//
// Scenario: N actors each submit M actor send packets through the Entry Spot
// dispatch path (ZLinkActorDispatchRouter -> ZLinkSpotActorDispatcher with a
// realistic Entry Spot send handler). Measures wall total, throughput, and
// per-packet completion latency p50/p95/p99.
//
// The script runs against the built framework (packages/framework/dist). When
// the build exposes the unified Entry Spot serial dispatch surface
// (ZLinkEntrySpotActivation.serialExecutor + ZLinkActorDispatchRouter entry
// executor option), the benchmark wires it the way the runtime does; on older
// builds it falls back to the legacy per-actor mailbox-only path. This keeps
// the same script usable for baseline vs patched comparison.

const path = require('node:path');
const { performance } = require('node:perf_hooks');

const framework = require(path.resolve(__dirname, '../packages/framework/dist/internal'));

const ACTOR_COUNT = Number(process.env.PERF_ACTORS ?? 200);
const PACKETS_PER_ACTOR = Number(process.env.PERF_PACKETS ?? 50);
const WARMUP_PACKETS = 2000;
const ROUNDS = Number(process.env.PERF_ROUNDS ?? 5);

class PlayerActor {
  constructor(actorId, context) {
    this.actorId = actorId;
    this.context = context;
  }
}

class PlayerFactory {
  create(actorId, context) {
    return new PlayerActor(actorId, context);
  }
}

class AdmissionEntrySpot {
  constructor() {
    this.admittedPackets = 0;
    this.pendingAdmissions = new Map();
  }
}

class AdmissionPacketHandler {
  async handle(spot, actor, context, message) {
    // Realistic admission bookkeeping: shared Entry Spot state mutation.
    spot.admittedPackets += 1;
    spot.pendingAdmissions.set(actor.actorId, { packetName: context.packetName, message });
    spot.pendingAdmissions.delete(actor.actorId);
  }
}

async function createFixture() {
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]])
  });
  const actors = [];
  for (let index = 0; index < ACTOR_COUNT; index += 1) {
    actors.push(await manager.create(`player-${index}`, 'player'));
  }

  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: AdmissionEntrySpot,
    nativeSpot: { routingId: 'entry-perf', async dispose() {} },
    nodeRid: 'node-perf',
    spotNodeName: 'node-perf'
  });
  await activation.create();

  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime().addPacket({
    kind: framework.ZLinkActorPacketKind.Send,
    packetName: 'admission.move',
    actorType: PlayerActor,
    handlerType: AdmissionPacketHandler
  });

  const entryExecutor = activation.serialExecutor;
  const unified = entryExecutor !== undefined;
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry,
    spot: activation.entrySpot,
    serial: unified ? entryExecutor : undefined
  });
  const router = unified
    ? new framework.ZLinkActorDispatchRouter(manager, { entryExecutor })
    : new framework.ZLinkActorDispatchRouter(manager);

  return { manager, activation, dispatcher, router, actors, unified };
}

async function runBurst(fixture, packetsPerActor, recordLatencies) {
  const { dispatcher, router, actors } = fixture;
  const latencies = recordLatencies ? [] : undefined;
  const submissions = [];
  const startWall = performance.now();
  for (let packet = 0; packet < packetsPerActor; packet += 1) {
    for (const actor of actors) {
      const startedAt = performance.now();
      const submission = router
        .submit(actor.actorId, (snapshot) =>
          dispatcher.dispatchSend(snapshot.actor, 'admission.move', packet))
        .then(() => {
          if (latencies !== undefined) {
            latencies.push(performance.now() - startedAt);
          }
        });
      submissions.push(submission);
    }
  }
  await Promise.all(submissions);
  const wallMs = performance.now() - startWall;
  return { wallMs, latencies };
}

function percentile(sortedValues, ratio) {
  const index = Math.min(sortedValues.length - 1, Math.ceil(sortedValues.length * ratio) - 1);
  return sortedValues[Math.max(0, index)];
}

async function main() {
  const fixture = await createFixture();
  const totalPackets = ACTOR_COUNT * PACKETS_PER_ACTOR;

  // Warmup.
  await runBurst(fixture, Math.ceil(WARMUP_PACKETS / ACTOR_COUNT), false);

  const results = [];
  for (let round = 0; round < ROUNDS; round += 1) {
    const { wallMs, latencies } = await runBurst(fixture, PACKETS_PER_ACTOR, true);
    latencies.sort((left, right) => left - right);
    results.push({
      wallMs,
      throughput: totalPackets / (wallMs / 1000),
      p50: percentile(latencies, 0.5),
      p95: percentile(latencies, 0.95),
      p99: percentile(latencies, 0.99)
    });
  }

  const median = (selector) => {
    const values = results.map(selector).sort((left, right) => left - right);
    return values[Math.floor(values.length / 2)];
  };

  console.log(`entry-spot admission burst (${fixture.unified ? 'unified-serial' : 'legacy-mailbox'} dispatch)`);
  console.log(`actors=${ACTOR_COUNT} packetsPerActor=${PACKETS_PER_ACTOR} totalPackets=${totalPackets} rounds=${ROUNDS}`);
  for (const [index, result] of results.entries()) {
    console.log(
      `round ${index + 1}: wall=${result.wallMs.toFixed(2)}ms ` +
      `throughput=${Math.round(result.throughput)}/s ` +
      `p50=${(result.p50).toFixed(3)}ms p95=${(result.p95).toFixed(3)}ms p99=${(result.p99).toFixed(3)}ms`
    );
  }
  console.log(
    `median: wall=${median((r) => r.wallMs).toFixed(2)}ms ` +
    `throughput=${Math.round(median((r) => r.throughput))}/s ` +
    `p50=${median((r) => r.p50).toFixed(3)}ms ` +
    `p95=${median((r) => r.p95).toFixed(3)}ms ` +
    `p99=${median((r) => r.p99).toFixed(3)}ms`
  );
  const expectedPackets = totalPackets * ROUNDS + Math.ceil(WARMUP_PACKETS / ACTOR_COUNT) * ACTOR_COUNT;
  if (fixture.activation.entrySpot.admittedPackets !== expectedPackets) {
    console.error(
      `packet count mismatch: ${fixture.activation.entrySpot.admittedPackets} !== ${expectedPackets}`
    );
    process.exit(1);
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
