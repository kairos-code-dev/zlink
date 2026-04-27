import * as zlink from '../src/canonical';

const ctx = new zlink.Context();
const routingId = zlink.RoutingId.fromBytes(Buffer.from('id'));
const peerRoutingId = zlink.RoutingId.fromBytes(Buffer.from('peer'));
const SERVICE_TYPE_SPOT = 0x3002;
ctx.shutdown();
ctx.options.ioThreads = ctx.options.ioThreads;
ctx.options.maxSockets = ctx.options.maxSockets;
ctx.options.maxMsgSize = ctx.options.maxMsgSize;
ctx.options.threadPriority = ctx.options.threadPriority;
ctx.options.threadSchedulingPolicy = ctx.options.threadSchedulingPolicy;
ctx.options.blocky = ctx.options.blocky;
ctx.options.addThreadAffinity(0);
ctx.options.removeThreadAffinity(0);
ctx.options.socketLimit;
ctx.options.msgTSize;

const registry = new zlink.Registry(ctx);
registry.setTlsServer('cert', 'key');
registry.setTlsClient('ca', 'host');

const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'surface-discovery');
discovery.setTlsClient('ca', 'host');
discovery.setDealerPeerMode(zlink.DiscoveryDealerPeerMode.Router);
discovery.resolveSpot(routingId);
discovery.memberPeers();
discovery.memberPeerMetadata(2, 'tcp://127.0.0.1:5555');
zlink.MonitorSocket.ignoreHandler(new zlink.MonitorEvent({ event: 0, value: 0 }));

const pub = new zlink.PubSocket(ctx);
pub.publish('topic', 'ok');
pub.onSendReady(() => {});
pub.attachDiscovery(new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'pub-service'));
pub.setTlsServer('cert', 'key');
pub.setTlsClient('ca', 'host');
pub.options.verbose = true;

const sub = new zlink.SubSocket(ctx);
sub.setSubscription('topic');
sub.subscribe();
sub.attachDiscovery(new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'sub-service'));
sub.setTlsServer('cert', 'key');
sub.setTlsClient('ca', 'host');
sub.options.topicsCount;

const dealer = new zlink.DealerSocket(ctx);
dealer.send('ok');
dealer.recv();
dealer.onSendReady(() => {});
dealer.setRoutingId(routingId);
dealer.getRoutingId();
dealer.attachDiscovery(new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'dealer-service'));
dealer.setTlsServer('cert', 'key');
dealer.setTlsClient('ca', 'host');
dealer.options.probe = true;

const router = new zlink.RouterSocket(ctx);
router.recv();
router.onSendReady(() => {});
router.setRoutingId(routingId);
router.getRoutingId();
router.attachDiscovery(new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'router-service'));
router.setTlsServer('cert', 'key');
router.setTlsClient('ca', 'host');
router.send(routingId, 'ok');
router.reply(routingId, 1n, 'ok');
router.options.mandatory = true;
router.options.connectRoutingId = peerRoutingId;

const stream = new zlink.StreamSocket(ctx);
stream.send(routingId, 'ok');
stream.recv();
stream.disconnectRid(peerRoutingId);
stream.onPacket((sourceRid, header, body) => {
  sourceRid.toString();
  header.data();
  body.data();
});
stream.onSendReady(() => {});
stream.setRoutingId(routingId);
stream.getRoutingId();
stream.setTlsServer('cert', 'key');
stream.setTlsClient('ca', 'host');
stream.options.notify = true;

const xpub = new zlink.XPubSocket(ctx);
xpub.publish('topic', 'ok');
xpub.receiveSubscriptionEvent();
xpub.onSendReady(() => {});
xpub.options.verbose = true;
xpub.options.verboser = true;
xpub.options.noDrop = true;
xpub.options.manual = true;

const spotNode = new zlink.SpotNode(ctx);
spotNode.setTlsServer('cert', 'key');
spotNode.setTlsClient('ca', 'host');
spotNode.setRoutingId(routingId);
spotNode.routingId;
const spot = spotNode.createSpot();
spot.setRoutingId(routingId);
spot.routingId;
spot.publish('svc', 'topic', 'ok');
spot.sendChannel('svc', 'ok');
spot.requestChannel('svc', 'ok');
spot.setSubscription('topic');
spot.unsetSubscription('topic');
spot.subscribe();
spot.receiveSubscriptionEvent();
spot.onSendReady(() => {});
spot.setLinger(0);
spot.setSendHighWaterMark(1);
spot.setReceiveHighWaterMark(1);
spot.setSendTimeout(1);
spot.setReceiveTimeout(1);
spot.setNoDrop(true);
spotNode.attachPubIngress(pub);

const monitor = pub.monitorOpen();
monitor.recv();
monitor.onEvent(() => {});
monitor.snapshot();
monitor.close();

const poller = new zlink.Poller();
poller.size;
poller.close();

const diagnosticMessage = zlink.Message.from(Buffer.from('diagnostic'));
const constructedMessage = new zlink.Message(Buffer.from('constructed'));
diagnosticMessage.data();
diagnosticMessage.size();
diagnosticMessage.getProperty('Socket-Type');
diagnosticMessage.refCount();
diagnosticMessage.close();
constructedMessage.data();
constructedMessage.close();
using _diagnosticDispose = diagnosticMessage;

registry.close();
discovery.close();
spot.close();
spotNode.close();
stream.close();
router.close();
dealer.close();
sub.close();
xpub.close();
pub.close();
ctx.close();
