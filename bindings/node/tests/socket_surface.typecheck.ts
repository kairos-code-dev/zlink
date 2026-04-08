import * as zlink from '../dist/index';

const ctx = new zlink.Context();
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

const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'surface-discovery');
discovery.setTlsClient('ca', 'host');
const discoveryMonitor = discovery.monitorOpen();
discoveryMonitor.recv();
discoveryMonitor.tryRecv();
discoveryMonitor.onEvent(() => {});
discoveryMonitor.snapshot();
discoveryMonitor.close();

const pub = new zlink.PubSocket(ctx);
pub.publish('topic', 'ok');
pub.tryPublish('topic', 'ok');
pub.onSendReady(() => {});
pub.attachDiscovery(new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'pub-service'));
pub.setTlsServer('cert', 'key');
pub.setTlsClient('ca', 'host');
pub.options.linger = 0;
pub.options.verbose = true;
// @ts-expect-error pub cannot recv
pub.recv();
// @ts-expect-error pub cannot subscribe
pub.subscribe();
// @ts-expect-error pub cannot use generic send
pub.send('ok');
// @ts-expect-error canonical pub does not expose raw sockopt
pub.setSockOpt(zlink.SocketOption.LINGER, Buffer.alloc(4));
// @ts-expect-error canonical pub does not expose generic option api
pub.setOption(zlink.SocketOption.LINGER, Buffer.alloc(4));

const sub = new zlink.SubSocket(ctx);
sub.setSubscription('topic');
sub.subscribe();
sub.trySubscribe();
sub.onSubscribe(() => {});
sub.attachDiscovery(new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'sub-service'));
sub.setTlsServer('cert', 'key');
sub.setTlsClient('ca', 'host');
sub.options.recvTimeout = 10;
sub.options.topicsCount;
// @ts-expect-error sub cannot send
sub.send('bad');
// @ts-expect-error sub cannot set routing id
sub.setRoutingId(Buffer.from('id'));
// @ts-expect-error topicsCount is read-only
sub.options.topicsCount = 1;

const dealer = new zlink.DealerSocket(ctx);
dealer.send('ok');
dealer.trySend('ok');
dealer.recv();
dealer.tryRecv();
dealer.onReceive(() => {});
dealer.onSendReady(() => {});
dealer.setRoutingId(Buffer.from('id'));
dealer.getRoutingId();
dealer.attachDiscovery(new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'dealer-service'));
dealer.setTlsServer('cert', 'key');
dealer.setTlsClient('ca', 'host');
dealer.options.probe = true;
dealer.options.maxMsgSize = 1024n;
// @ts-expect-error dealer has no subscribe
dealer.subscribe();
// @ts-expect-error dealer has no generic option api
dealer.setOption(zlink.SocketOption.LINGER, Buffer.alloc(4));

const router = new zlink.RouterSocket(ctx);
router.recv();
router.tryRecv();
router.onReceive(() => {});
router.onSendReady(() => {});
router.setRoutingId(Buffer.from('id'));
router.getRoutingId();
router.attachDiscovery(new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'router-service'));
router.setTlsServer('cert', 'key');
router.setTlsClient('ca', 'host');
router.send(Buffer.from('id'), 'ok');
router.trySend(Buffer.from('id'), ['ok']);
router.options.mandatory = true;
router.options.connectRoutingId = Buffer.from('peer');
// @ts-expect-error router has no subscribe
router.subscribe();
// @ts-expect-error router has no generic sockopt api
router.setSockOpt(zlink.SocketOption.LINGER, Buffer.alloc(4));

const stream = new zlink.StreamSocket(ctx);
stream.send(Buffer.from('id'), 'ok');
stream.trySend(Buffer.from('id'), 'ok');
stream.recv();
stream.tryRecv();
stream.onReceive(() => {});
stream.onSendReady(() => {});
stream.setRoutingId(Buffer.from('id'));
stream.getRoutingId();
stream.setTlsServer('cert', 'key');
stream.setTlsClient('ca', 'host');
stream.options.notify = true;
// @ts-expect-error canonical stream has no streamAttach helper
stream.streamAttach(() => {});
// @ts-expect-error canonical stream cannot connect
stream.connect('tcp://127.0.0.1:5555');
// @ts-expect-error canonical stream has no generic option api
stream.setOption(zlink.SocketOption.LINGER, Buffer.alloc(4));

const xpub = new zlink.XPubSocket(ctx);
xpub.publish('topic', 'ok');
xpub.tryPublish('topic', 'ok');
xpub.receiveSubscriptionEvent();
xpub.tryReceiveSubscriptionEvent();
xpub.onSendReady(() => {});
xpub.options.verbose = true;
xpub.options.verboser = true;
xpub.options.noDrop = true;
xpub.options.manual = true;
// @ts-expect-error xpub cannot subscribe
xpub.subscribe();
// @ts-expect-error xpub cannot use generic send
xpub.send('ok');
// @ts-expect-error aligned xpub no longer exposes setter aliases
xpub.setVerbose(true);

const spotNode = new zlink.SpotNode(ctx);
// @ts-expect-error canonical spot node has no lastEndpoint alias
spotNode.lastEndpoint();
spotNode.setTlsServer('cert', 'key');
spotNode.setTlsClient('ca', 'host');
const spot = new zlink.Spot(spotNode);
spot.publish('topic', 'ok');
spot.tryPublish('topic', ['ok']);
spot.setSubscription('topic');
spot.unsetSubscription('topic');
spot.subscribe();
spot.trySubscribe();
spot.onSubscribe(() => {});
spot.onSendReady(() => {});
// @ts-expect-error spot does not expose recv
spot.recv();
// @ts-expect-error spot no longer exposes service monitor
spot.monitorOpen();
// @ts-expect-error spot node no longer exposes service monitor
spotNode.monitorOpen();

const monitor = pub.monitorOpen();
monitor.recv();
monitor.tryRecv();
monitor.onEvent(() => {});
monitor.close();

const diagnosticMessage = zlink.Message.fromBuffer(Buffer.from('diagnostic'));
diagnosticMessage.data;
diagnosticMessage.size;
diagnosticMessage.getProperty('Socket-Type');
diagnosticMessage.refCount();
diagnosticMessage.close();
using _diagnosticDispose = diagnosticMessage;
// @ts-expect-error canonical message no longer exposes copyOf
zlink.Message.copyOf('bad');
// @ts-expect-error canonical message no longer exposes wrap
zlink.Message.wrap(Buffer.from('bad'));
// @ts-expect-error canonical message no longer exposes empty
zlink.Message.empty();
// @ts-expect-error canonical message no longer exposes toBuffer
diagnosticMessage.toBuffer();
// @ts-expect-error canonical message no longer exposes byteLength
diagnosticMessage.byteLength();

discovery.close();
registry.close();
ctx.close();
