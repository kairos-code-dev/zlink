const assert = require('node:assert/strict');
const test = require('node:test');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');

test('location key codec matches the dotnet canonical segment format', () => {
  const nodeRid = zlink.RoutingId.from('node-a');
  const spotId = '방:room-1';

  assert.equal(
    internal.ZLinkLocationKeyCodec.encodePeerKey({
      autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
      meshName: 'game',
      role: framework.ZLinkLocationRole.Router,
      nodeRid,
      endpoint: 'tcp://ignored'
    }),
    '10:route-mesh4:game6:router12:6e6f64652d61'
  );
  assert.equal(
    internal.ZLinkLocationKeyCodec.encodePeerKey({
      autoConnectType: framework.ZLinkLocationAutoConnectType.Fanout,
      meshName: 'events',
      role: framework.ZLinkLocationRole.Sub,
      endpoint: 'tcp://127.0.0.1:7001'
    }),
    '6:fanout6:events3:sub20:tcp://127.0.0.1:7001'
  );
  assert.equal(
    internal.ZLinkLocationKeyCodec.encodeSpotKey({ meshName: 'game', spotId }),
    '4:game10:방:room-1'
  );
  assert.equal(
    internal.ZLinkLocationKeyCodec.encodeActorKey({ meshName: 'play', actorId: 'alice' }),
    '4:play5:alice'
  );
  assert.equal(
    internal.ZLinkLocationKeyCodec.encodeRouteKey({
      routeKind: framework.ZLinkRouteKind.ActorSession,
      routeKey: 'session-1'
    }),
    '1:19:session-1'
  );
});

test('location canonical names reject values outside the shared contract', () => {
  assert.equal(
    internal.zlinkLocationAutoConnectTypeName(framework.ZLinkLocationAutoConnectType.ClientServer),
    'client-server'
  );
  assert.equal(internal.zlinkLocationRoleName(framework.ZLinkLocationRole.Dealer), 'dealer');
  assert.equal(
    internal.tryParseZLinkLocationAutoConnectType('spot-mesh'),
    framework.ZLinkLocationAutoConnectType.SpotMesh
  );
  assert.equal(internal.tryParseZLinkLocationRole('pub'), framework.ZLinkLocationRole.Pub);
  assert.throws(() => internal.zlinkLocationAutoConnectTypeName(999), /Unknown location auto-connect type/);
  assert.throws(() => internal.zlinkLocationRoleName(999), /Unknown location role/);
  assert.equal(framework.zlinkLocationAutoConnectTypeName, undefined);
  assert.equal(framework.zlinkLocationRoleName, undefined);
});
