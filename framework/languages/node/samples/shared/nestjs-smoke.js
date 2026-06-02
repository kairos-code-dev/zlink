const defaultNestjs = require('../../packages/nestjs/dist');

function assertNestModule(options, nestjs = defaultNestjs) {
  const module = nestjs.ZLinkModule.forRoot(options);
  const tokens = new Set(module.providers.map((provider) => provider.provide));
  for (const token of [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_CHANNEL_CLIENT,
    nestjs.ZLINK_ROUTE_CLIENT,
    nestjs.ZLINK_FANOUT_CLIENT
  ]) {
    if (!tokens.has(token)) {
      throw new Error(`NestJS ZLinkModule provider is missing: ${String(token)}`);
    }
  }
  return module;
}

module.exports = { assertNestModule };
