// SPDX-License-Identifier: MPL-2.0

const normalize = value => value.toLowerCase()
  .replace(/[^a-z0-9]+/gu, '');

export const semanticMemberKey = member => [
  member.language,
  normalize(member.ownerIdentity),
  normalize(member.memberName),
  member.kind,
].join('\0');

const portableOwner = ownerIdentity => {
  const simple = ownerIdentity.split(/::|\./u).at(-1) ?? ownerIdentity;
  const normalized = normalize(simple).replace(/^izlink|^zlink/gu, '');
  return /_t$/u.test(simple) ? normalized.slice(0, -1) : normalized;
};

export const removedMemberParityKey = member => {
  const owner = portableOwner(member.ownerIdentity);
  const name = normalize(member.memberName);
  if (member.language === 'kotlin' && /package|extensionskt/u.test(owner)) {
    return `kotlin-logical.${name}`;
  }
  if (/entryspotoptions/u.test(owner) && /^(?:set)?routingid$/u.test(name)) {
    return 'entry-spot-options.routing-id';
  }
  if (/meshnodebuilder/u.test(owner) && name === 'setentryspotroutingid') {
    return 'entry-spot-options.routing-id';
  }
  if (/instancespotfactoryoptions/u.test(owner) && name === 'maxactiveinstances') {
    return 'instance-spot-factory-options.max-active-instances';
  }
  if (/instancespotfactoryoptions/u.test(owner) && /activationtimeout(?:ms)?/u.test(name)) {
    return 'instance-spot-factory-options.activation-timeout';
  }
  if (/meshnodebuilder/u.test(owner) && /^(?:add)?actorfactory$/u.test(name)) {
    return 'mesh-node-builder.actor-factory-registration';
  }
  if (/meshNodeDescriptor/i.test(owner) && name === 'spottypes') {
    return 'mesh-node-descriptor.spot-types';
  }
  if (/objectcapability/u.test(owner) && name === 'available') {
    return 'object-capability.available';
  }
  return `${owner}.${name}.${member.kind}`;
};

export const replacementParitySignature = behavior => JSON.stringify({
  decisions: [...behavior.decisionCoverage].sort(),
  coverage: behavior.replacementCoverage
    .map(id => id.replace(/:(?:cpp|dotnet|java|node)$/u, ':<runtime>'))
    .sort(),
});

const runtimeLanguage = language => language === 'kotlin' ? 'java' : language;

const normalizedOwner = member => normalize(member.ownerIdentity);
const normalizedMemberName = member => normalize(member.memberName);
const isKotlinSourcePackageMember = member => member.language === 'kotlin'
  && member.ownerIdentity.includes('::<package>');

export const closedCatchAllExpectations = {
  'kotlin-reviewed-contract-set': {
    count: 95,
    identitySetSha256: '5937b0bd1328b8842b0a6377d77b9d926d4189e3870ede85d691c233f5047c0c',
  },
  'node-reviewed-contract-set': {
    count: 54,
    identitySetSha256: '28b3aaa4f7a606fbe54b4538e7d293785c7879976f077ffed2c958a7c763e208',
  },
};

export const sourceJvmParityExpectation = {
  groups: 51,
  recoveredPairs: 47,
  identitySetSha256: '80709b4b015cd3b082dca1894c3659a5616f13f1104e0b25526c15b4d2887b75',
};

const rules = [
  {
    id: 'publish-monitoring-removal',
    matches: (_value, member) => {
      const owner = normalizedOwner(member);
      const name = normalizedMemberName(member);
      const targetCount = /^(?:remote|local)(?:snapshot|admitted|dropped)count$/u.test(name)
        || /^(?:target|drop)count$/u.test(name);
      return /logicalmulticastsnapshot(?:t)?$/u.test(owner)
        || (/meshnodesnapshot(?:t)?$/u.test(owner) && name === 'multicast')
        || (/(?:meshruntimeevent|messageflowevent)(?:t)?$/u.test(owner) && targetCount);
    },
    decisions: ['CA-D77'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'deferred-actor-join',
    matches: (_value, member) => {
      const owner = normalizedOwner(member);
      const name = normalizedMemberName(member);
      return /actorjoin(?:call|result)/u.test(owner)
        || name === 'typedactorjoinresultt'
        || (isKotlinSourcePackageMember(member)
          && /^(?:awaitjoin|awaitjoinreply)$/u.test(name));
    },
    decisions: ['CA-D74'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'object-context-composition',
    matches: (_value, member) => {
      const owner = normalizedOwner(member);
      const name = normalizedMemberName(member);
      return (portableOwner(member.ownerIdentity) === 'actor' && name === 'actorid')
        || (/zlinkentryspot$/u.test(owner) && name === 'context');
    },
    decisions: ['CA-D75'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'unified-message-context',
    matches: (_value, member) => {
      const owner = normalizedOwner(member);
      return /(?:handlercontextt|zlinkhandlercontext|routehandlercontextt|zlinkroutesendcontext|zlinkrouterequestcontext|zlinksendcontext|zlinkrequestcontext|publishcontextt|zlinkpublishcontext|spotactorsendcontextt|spotactorrequestcontextt|spotactorreplyoptionst|zlinkspotactorsendcontext|zlinkspotactorrequestcontext|zlinkspotactorreplyoptions|zlinksessiondispatchcontext|handlerinvocationcontextt|zlinkhandlerinvocation|zlinkinvocationcontext)$/u.test(owner);
    },
    decisions: ['CA-D76'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'one-way-result-removal',
    matches: (_value, member) =>
      /(?:logicalmulticastdetail|publishresult|submitresult|submitstatus)(?:t)?$/u
        .test(normalizedOwner(member)),
    decisions: ['CA-D72'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'one-way-terminator-rename',
    matches: (_value, member) => {
      const owner = normalizedOwner(member);
      const name = normalizedMemberName(member);
      const dotnetOneWayCall = member.language === 'dotnet'
        && /izlink(?:actor)?sendcall$|izlinkboundsessionsendcall$|izlinkfanoutpublishcall$|izlinkpublishcall$|izlinksessionsendcall$|izlinksessionreplycall$/u.test(owner)
        && name === 'submitasync';
      const kotlinOneWay = member.language === 'kotlin'
        && (/zlinkframeworkextensionskt$/u.test(owner) || isKotlinSourcePackageMember(member))
        && /^(?:send|publishtotopic)$/u.test(name);
      return dotnetOneWayCall || kotlinOneWay;
    },
    decisions: ['CA-D72', 'CA-D73'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'async-terminator-rename',
    matches: (_value, member) => {
      const owner = normalizedOwner(member);
      const name = normalizedMemberName(member);
      const cppOrNodeCall = /(?:actorrequestcallt|channelrequestcallt|requestcallt|workercallt|zlinkworkercall)$/u
        .test(owner) && /^(?:async|asyncmessage)$/u.test(name);
      const kotlinSourceCall = isKotlinSourcePackageMember(member)
        && /^(?:awaitreply|request|requesttoactorawait|yieldreply|yieldworker)$/u.test(name);
      const kotlinJvmCall = member.language === 'kotlin'
        && /zlinkframeworkextensionskt$/u.test(owner)
        && /^(?:awaitreply|request|requesttoactorawait|yieldreply|yieldworker)$/u.test(name);
      return cppOrNodeCall || kotlinSourceCall || kotlinJvmCall;
    },
    decisions: ['CA-D73'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'redis-transfer-store-split',
    matches: value => /redis.*checkpoint|checkpoint.*redis/u.test(value),
    decisions: ['CA-D31', 'CA-D34', 'CA-D36'],
    coverage: () => ['e2e:add:redis-stores-shared-deployment'],
  },
  {
    id: 'transfer-store-vocabulary',
    matches: value => /checkpoint|transferstore|transferreference|transferstored/u.test(value)
      && !/redis/u.test(value),
    decisions: ['CA-D31', 'CA-D36'],
    coverage: () => ['e2e:add:relocation-store-required-registration'],
  },
  {
    id: 'relocation-vocabulary-breaking-rename',
    matches: value => /transfer/u.test(value)
      && !/checkpoint|transferstore|transferreference|transferstored|redis|forward|routecache/u.test(value),
    decisions: ['CA-D36', 'CA-D37', 'CA-D38', 'CA-D39', 'CA-D40', 'CA-D41', 'CA-D42', 'CA-D43'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'instance-spot-explicit-create',
    matches: value => /instancespotaddress|requesttospot|sendtospot/u.test(value)
      && !/spotrid/u.test(value),
    decisions: ['CA-D25'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'automatic-routing-id',
    matches: value => /routingidslot|allocatedroutingid|routingidallocation|useallocatedroutingid|setroutingidallocation/u.test(value),
    decisions: ['CA-D20', 'CA-D21'],
    coverage: () => ['e2e:add:automatic-rid-collision'],
  },
  {
    id: 'exact-session-bind',
    matches: value => /sessionactors.*bind|bindasync|bindactor|enableactordispatch/u.test(value),
    decisions: ['CA-D05', 'CA-D06'],
    coverage: () => ['e2e:add:exact-generation-mutation-bind'],
  },
  {
    id: 'exact-object-mutation',
    matches: value => /destroy|closespot|closeasync|spotmanagerclose/u.test(value),
    decisions: ['CA-D27'],
    coverage: () => ['e2e:add:exact-generation-mutation-bind'],
  },
  {
    id: 'opaque-object-capability',
    matches: value => /objectcapability(?:.*)(?:readablestatecontractids|type)/u.test(value),
    decisions: ['CA-D14', 'CA-D37', 'CA-D39'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'spot-context-logical-identity',
    matches: value => /spotcommoncontext(?:.*)(?:spotname|routingid)/u.test(value),
    decisions: ['CA-D01', 'CA-D02'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'spot-id-string-identity',
    matches: value => /spotrid/u.test(value),
    decisions: ['CA-D65'],
    coverage: member => [
      'e2e:add:global-spot-explicit-create',
      `public-behavior:formal-contract-parity:${member.language}`,
    ],
  },
  {
    id: 'yield-surface-restriction',
    matches: (value, member) => /yield/u.test(value)
      && !/actorjoin/u.test(normalizedOwner(member))
      && !(isKotlinSourcePackageMember(member)
        && /^(?:yieldreply|yieldworker)$/u.test(normalizedMemberName(member)))
      && !(member.language === 'kotlin'
        && /zlinkframeworkextensionskt$/u.test(normalizedOwner(member))
        && /^(?:yieldreply|yieldworker)$/u.test(normalizedMemberName(member))),
    decisions: ['CA-D58', 'CA-D59'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'spot-actor-lifecycle-split',
    matches: value => /(?:entryspot|spotactorlifecycle).*(?:onactorjoin|onjoinedactor|onleaveactor|ondisconnectactor)/u.test(value),
    decisions: ['CA-D67'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'authority-create-transition-split',
    matches: value => /authorityexpect(?:missing|found)|authorityexpectationzlinkauthorityexpectation|authoritygenerationtransition.*newobject|missingmissing/u.test(value)
      && !/equals|hashcode|tostring/u.test(value),
    decisions: ['CA-D44', 'CA-D45'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'reviewed-exact-surface-cleanup',
    matches: (value, member) =>
      /zlinkentryspotcontext|zlinkframeworkruntimeeventruntime|zlinkhandlerinvocation(?:context|message)/u
        .test(value)
      && !/spotrid|zlinkhandlerinvocation(?:context|message)/u.test(value)
      && !(/zlinkentryspot$/u.test(normalizedOwner(member))
        && normalizedMemberName(member) === 'context'),
    decisions: ['CA-D29'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'actor-spot-relocation',
    matches: (value, member) => /joinentryspot/u.test(value)
      && !/actorjoin/u.test(normalizedOwner(member)),
    decisions: ['CA-D07'],
    coverage: () => ['e2e:add:same-node-join-without-relocation-payload'],
  },
  {
    id: 'session-actor-bind-reference',
    matches: value => /sessionactormanager|sessionactor(?:t|\b)/u.test(value),
    decisions: ['CA-D05'],
    coverage: () => ['e2e:add:exact-generation-mutation-bind'],
  },
  {
    id: 'actor-ref-record-helper',
    matches: value => /actorrefsnapshot.*(?:equals|hashcode|tostring)/u.test(value),
    decisions: ['CA-D29'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'actor-ref-contract',
    matches: value => /actorrefsnapshot|actorref/u.test(value)
      && !/iszlinkframeworkerrorretriablebydefault|equals|hashcode|tostring/u.test(value),
    decisions: ['CA-D01'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'actor-directory-query',
    matches: value => /actordirectory|findactor/u.test(value),
    decisions: ['CA-D26'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'actor-spot-handle-query',
    matches: value => /actorspothandleresolver|resolveactorspothandle/u.test(value),
    decisions: ['CA-D01', 'CA-D26'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'spot-manager-replacement',
    matches: value => /spothandleresolver|resolvespothandle|spothandle|spotmanager.*(?:find|list)|spotinfo/u.test(value)
      && !/actorspothandleresolver|resolveactorspothandle|spotrid/u.test(value),
    decisions: ['CA-D02', 'CA-D26'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'actor-global-authority-key',
    matches: value => /actorlocationkey|authoritysnapshot.*actor/u.test(value),
    decisions: ['CA-D01'],
    coverage: member => [`regression:add:global-authority-key:${runtimeLanguage(member.language)}`],
  },
  {
    id: 'spot-global-authority-key',
    matches: value => /spotlocationkey|authoritysnapshot.*spot/u.test(value)
      && !/spotrid/u.test(value),
    decisions: ['CA-D02'],
    coverage: member => [`regression:add:global-authority-key:${runtimeLanguage(member.language)}`],
  },
  {
    id: 'instance-spot-activation-timeout',
    matches: value => /instancespotfactoryoptions.*(?:activationtimeout|fromseconds)/u.test(value),
    decisions: ['CA-D04', 'CA-D10'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'instance-spot-options-record-helper',
    matches: value => /instancespotfactoryoptions.*(?:equals|hashcode|tostring)/u.test(value),
    decisions: ['CA-D29'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'instance-spot-options-constructor',
    matches: value => /instancespotfactoryoptions.*instancespotfactoryoptions/u.test(value),
    decisions: ['CA-D04', 'CA-D10', 'CA-D23'],
    coverage: () => [
      'e2e:add:global-spot-explicit-create',
      'e2e:add:placement-capacity-weight',
    ],
  },
  {
    id: 'instance-spot-active-capacity',
    matches: value => /instancespotfactoryoptions.*maxactiveinstances/u.test(value),
    decisions: ['CA-D23'],
    coverage: () => ['e2e:add:placement-capacity-weight'],
  },
  {
    id: 'mesh-node-spot-types',
    matches: value => /meshNodeDescriptor.*spotTypes/i.test(value),
    decisions: ['CA-D14'],
    coverage: () => ['e2e:add:placement-capacity-weight'],
  },
  {
    id: 'object-capability-availability',
    matches: value => /objectcapability.*available/u.test(value),
    decisions: ['CA-D23'],
    coverage: () => ['e2e:add:placement-capacity-weight'],
  },
  {
    id: 'placement-policy',
    matches: value => /placement|capacity|weight/u.test(value)
      && !/zlinksocket/u.test(value),
    decisions: ['CA-D14', 'CA-D22', 'CA-D23', 'CA-D70'],
    coverage: () => ['e2e:add:placement-capacity-weight'],
  },
  {
    id: 'reservation-recovery',
    matches: value => /reservation|reserve|commitreservation|abortreservation/u.test(value),
    decisions: ['CA-D24'],
    coverage: () => ['e2e:add:reservation-crash-recovery'],
  },
  {
    id: 'forwarding-and-cache',
    matches: value => /forwardwindow|forwarding|routecache|route.*cache/u.test(value),
    decisions: ['CA-D16', 'CA-D17'],
    coverage: () => ['e2e:add:forwarding-bounds'],
  },
  {
    id: 'actor-factory-registration',
    matches: value => /meshnodebuilder.*(?:add)?actorfactory/u.test(value),
    decisions: ['CA-D14', 'CA-D18', 'CA-D28'],
    coverage: () => ['sample:add:remote-object-create'],
  },
  {
    id: 'actor-client-messaging',
    matches: value => /actorclient.*(?:requesttoactor|sendtoactor)/u.test(value),
    decisions: ['CA-D01', 'CA-D26'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'actor-fluent-create',
    matches: value => /actormanager.*create|actormanager.*getorcreate/u.test(value),
    decisions: ['CA-D01', 'CA-D04', 'CA-D09'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'spot-fluent-create',
    matches: (value, member) => /spotmanager.*create|spotmanager.*getorcreate|spotcreate/u.test(value)
      && !/meshnodebuilder|nestmeshnodebuilder/u.test(normalize(member.ownerIdentity))
      && !/maxactiveinstances|spotrid/u.test(value),
    decisions: ['CA-D02', 'CA-D04', 'CA-D09'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'closed-object-role',
    matches: value => /addentryspot|configureentryspot|entryspotoptions|meshnodebuilder.*add.*spot/u.test(value)
      && !/entryspotoptions.*routingid|setentryspotroutingid/u.test(value),
    decisions: ['CA-D18', 'CA-D28'],
    coverage: () => ['sample:add:remote-object-create'],
  },
  {
    id: 'operational-location-query',
    matches: value => /peerlocationresolver|listlivepeers|locationruntimequery|list.*location|listtopology|listmeshnode|listauthorities|readauthority|listclientservers|listfanoutpublishers|listpeers|listroutes/u.test(value)
      && !/resolve.*handle/u.test(value),
    decisions: ['CA-D26'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'fixed-entry-routing-id',
    matches: value => /entryspotoptions.*routingid|setentryspotroutingid/u.test(value),
    decisions: ['CA-D19'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'kotlin-reviewed-contract-set',
    matches: (value, member) => member.language === 'kotlin'
      && /extensionskt|zlinksuspending|package/u.test(value),
    decisions: ['CA-D29'],
    coverage: () => ['public-behavior:formal-contract-parity:kotlin'],
  },
  {
    id: 'actor-location-filter',
    matches: value => /actorlocationfilter/u.test(value)
      && !/spotrid/u.test(value),
    decisions: ['CA-D01', 'CA-D26'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'spot-location-filter',
    matches: value => /spotlocationfilter/u.test(value)
      && !/spotrid/u.test(value),
    decisions: ['CA-D02', 'CA-D26'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'node-reviewed-contract-set',
    matches: (value, member) => member.language === 'node'
      && /zlinksocket|zlinkspotevent|zlinkdecoratormetadata|zlinkframeworkerrorkindvalues|zlinkspotactorrequest|zlinkspotactorreplyoptions|zlinkspotpeer|zlinksession|zlinkactorjoinresult|iszlinkframeworkerrorretriablebydefault/i.test(value)
      && !/zlinkactorjoinresult|zlinksessiondispatchcontext|zlinkspotactorreplyoptions|zlinkspotactorrequestcontext/i.test(value),
    decisions: ['CA-D29'],
    coverage: () => ['public-behavior:formal-contract-parity:node'],
  },
  {
    id: 'generated-record-contract-set',
    matches: value => /zlinkmeshNodeDescriptorhashset|zlinkauthorityexpectmissing(?:equals|hashcode|tostring)/i.test(value),
    decisions: ['CA-D29'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'cpp-reviewed-contract-set',
    matches: (value, member) => member.language === 'cpp'
      && /(?:actorlocation|spotlocation|routelocation).*from|appt(?:retire|shutdown)|(?:clientserverchannelserverbuilder|meshchannelserverbuilder|meshnodebuilder).*(?:addrequesthandler|addsendhandler|addrouterequesthandler|addroutesendhandler)|healthbuildertsetstatus|loggert(?:critical|debug|error|info|log|trace|warn)|loggingbuildert(?:useasync|userotatingfile)|metricsbuildertrecordruntimemetric|requestclientt(?:request|send)|spotcommoncontexttaddtimer/u.test(value)
      && !/spotlocationkey/u.test(value),
    decisions: ['CA-D29'],
    coverage: () => ['public-behavior:formal-contract-parity:cpp'],
  },
  {
    id: 'node-generated-contract-set',
    matches: (value, member) => member.language === 'node'
      && /zlinkauthoritykeybrand|zlinkauthorityversionbrand|zlinkauthoritykeytrue|zlinkauthoritystoreversiontrue|zlinkmoduleoptionstrue|zlinkmetercreate(?:counter|histogram|updowncounter)/u.test(value),
    decisions: ['CA-D29'],
    coverage: () => ['public-behavior:formal-contract-parity:node'],
  },
  {
    id: 'redis-location-options-split',
    matches: value => /mutable.*redislocationoptions/u.test(value),
    decisions: ['CA-D34'],
    coverage: () => ['e2e:add:redis-stores-shared-deployment'],
  },
];

export function auditRemovedMemberBehavior(member) {
  const value = normalize(`${member.ownerIdentity}.${member.memberName}`);
  const kotlinReviewedRule = rules.find(rule => rule.id === 'kotlin-reviewed-contract-set');
  const domainMatches = rules.filter(rule => rule !== kotlinReviewedRule)
    .filter(candidate => candidate.matches(value, member));
  const matches = domainMatches.length > 0
    ? domainMatches
    : kotlinReviewedRule.matches(value, member) ? [kotlinReviewedRule] : [];
  if (matches.length !== 1) {
    return {
      state: matches.length === 0 ? 'unmatched' : 'ambiguous',
      ruleIds: matches.map(rule => rule.id),
    };
  }
  const [rule] = matches;
  return {state: 'matched', behavior: {
    ruleId: rule.id,
    decisionCoverage: rule.decisions,
    replacementCoverage: rule.coverage(member),
  }};
}

export function removedMemberBehavior(member) {
  const audit = auditRemovedMemberBehavior(member);
  if (audit.state !== 'matched') {
    throw new Error(`${audit.state}:${audit.ruleIds.join(',')} removed public member: ${member.identity}`);
  }
  return audit.behavior;
}
