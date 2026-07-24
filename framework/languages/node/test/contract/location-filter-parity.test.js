const assert = require('node:assert/strict');
const test = require('node:test');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const core = require('../../packages/framework/dist/location-store-integration');
const redis = require('../../packages/framework-locations-redis/dist/location-filter-predicates');

test('in-memory and Redis location stores apply the same filter semantics', () => {
  const nodeRid = rid('node-1');
  const otherNodeRid = rid('node-2');
  const spotId = rid('spot-1');
  const otherSpotId = rid('spot-2');

  assertMatcherParity(core.matchesPeerLocation, redis.matchesPeer, {
    autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
    meshName: 'play',
    role: framework.ZLinkLocationRole.Router,
    nodeRid,
    endpoint: 'tcp://127.0.0.1:5001'
  }, [
    {},
    { meshName: 'play', role: framework.ZLinkLocationRole.Router },
    { meshName: 'other' },
    { nodeRid },
    { nodeRid: otherNodeRid },
    { endpoint: 'tcp://127.0.0.1:5002' }
  ]);

  assertMatcherParity(core.matchesSpotLocation, redis.matchesSpot, {
    meshName: 'play',
    spotType: 'game',
    nodeRid,
    spotKind: framework.ZLinkSpotKind.User
  }, [
    {},
    { meshName: 'play', spotType: 'game' },
    { spotType: 'entry' },
    { nodeRid },
    { nodeRid: otherNodeRid },
    { spotKind: framework.ZLinkSpotKind.Entry }
  ]);

  assertMatcherParity(core.matchesActorLocation, redis.matchesActor, {
    actorType: 'player',
    nodeRid,
    spotId,
    locationKind: framework.ZLinkSpotKind.User
  }, [
    {},
    { actorType: 'player', locationKind: framework.ZLinkSpotKind.User },
    { actorType: 'room' },
    { nodeRid: otherNodeRid },
    { spotId },
    { spotId: otherSpotId }
  ]);

  assertMatcherParity(core.matchesRouteLocation, redis.matchesRoute, {
    routeKind: framework.ZLinkRouteKind.ActorSession,
    ownerNodeRid: nodeRid,
    ownerId: 'owner-a'
  }, [
    {},
    { routeKind: framework.ZLinkRouteKind.ActorSession, ownerId: 'owner-a' },
    { routeKind: framework.ZLinkRouteKind.SpotName },
    { ownerNodeRid: nodeRid },
    { ownerNodeRid: otherNodeRid },
    { ownerId: 'owner-b' }
  ]);
});

function assertMatcherParity(coreMatcher, redisMatcher, row, filters) {
  for (const filter of filters) {
    assert.equal(redisMatcher(row, filter), coreMatcher(row, filter));
  }
}

function rid(value) {
  return zlink.RoutingId.from(value);
}
