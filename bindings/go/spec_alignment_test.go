package zlink

import (
	"errors"
	"reflect"
	"strings"
	"testing"
)

func TestRoutingIDCanonicalHelpers(t *testing.T) {
	rid := NewRoutingID([]byte{0x00, 0x41, 0x42})
	if rid.Size() != 3 {
		t.Fatalf("Size() = %d, want 3", rid.Size())
	}
	if got := rid.Hex(); got != "004142" {
		t.Fatalf("Hex() = %q, want %q", got, "004142")
	}
	if got := rid.String(); got != "004142" {
		t.Fatalf("String() = %q, want %q", got, "004142")
	}
	if got := NewRoutingIDFromString("004142"); !got.Equal(rid) {
		t.Fatalf("NewRoutingIDFromString() = %v, want %v", got, rid)
	}
	parsed, err := ParseRoutingIDString("004142")
	if err != nil {
		t.Fatalf("ParseRoutingIDString() error = %v", err)
	}
	if !parsed.Equal(rid) {
		t.Fatalf("ParseRoutingIDString() = %v, want %v", parsed, rid)
	}
	maxParsed, err := ParseRoutingIDString(strings.Repeat("a", 510))
	if err != nil {
		t.Fatalf("ParseRoutingIDString(max) error = %v", err)
	}
	if maxParsed.Size() != 255 {
		t.Fatalf("ParseRoutingIDString(max).Size() = %d, want 255", maxParsed.Size())
	}
	if got := NewRoutingIDFromString("not-hex"); got.Size() != 0 {
		t.Fatalf("NewRoutingIDFromString(invalid).Size() = %d, want 0", got.Size())
	}
	if got := NewRoutingIDFromString(strings.Repeat("a", 512)); got.Size() != 0 {
		t.Fatalf("NewRoutingIDFromString(oversize).Size() = %d, want 0", got.Size())
	}
	if _, err := ParseRoutingIDString(strings.Repeat("a", 512)); err == nil {
		t.Fatalf("ParseRoutingIDString(oversize) should fail")
	} else {
		var configErr *ConfigError
		if !errors.As(err, &configErr) {
			t.Fatalf("ParseRoutingIDString(oversize) error type = %T, want *ConfigError", err)
		}
	}
	if rid.Hash() != NewRoutingID([]byte{0x00, 0x41, 0x42}).Hash() {
		t.Fatalf("Hash() should be stable for equal routing ids")
	}
	bytes := rid.Bytes()
	bytes[0] = 0xFF
	if got := rid.Bytes()[0]; got != 0x00 {
		t.Fatalf("Bytes() should return a defensive copy, got %#x", got)
	}
}

func TestReceivedAndTopicConvenienceHelpersUseRecvError(t *testing.T) {
	recv := &Received{}
	if _, err := recv.FirstPart(); err == nil {
		t.Fatalf("FirstPart() should fail on empty recv")
	} else {
		var recvErr *RecvError
		if !errors.As(err, &recvErr) {
			t.Fatalf("FirstPart() error type = %T, want *RecvError", err)
		}
	}
	if _, err := recv.SinglePartOrError(); err == nil {
		t.Fatalf("SinglePartOrError() should fail on empty recv")
	} else {
		var recvErr *RecvError
		if !errors.As(err, &recvErr) {
			t.Fatalf("SinglePartOrError() error type = %T, want *RecvError", err)
		}
	}

	topic := &TopicMessage{}
	if _, err := topic.FirstPart(); err == nil {
		t.Fatalf("TopicMessage.FirstPart() should fail on empty topic message")
	} else {
		var recvErr *RecvError
		if !errors.As(err, &recvErr) {
			t.Fatalf("TopicMessage.FirstPart() error type = %T, want *RecvError", err)
		}
	}
	if _, err := topic.SinglePartOrError(); err == nil {
		t.Fatalf("TopicMessage.SinglePartOrError() should fail on empty topic message")
	} else {
		var recvErr *RecvError
		if !errors.As(err, &recvErr) {
			t.Fatalf("TopicMessage.SinglePartOrError() error type = %T, want *RecvError", err)
		}
	}
	if topic.ServiceName() != "" || topic.HasServiceName() {
		t.Fatalf("TopicMessage zero value should not report a service name")
	}
}

func TestReceivedReplyHelpersCarryCanonicalMetadata(t *testing.T) {
	replyPart, err := NewMessage([]byte("reply"))
	if err != nil {
		t.Fatalf("NewMessage() error = %v", err)
	}
	defer replyPart.Close()

	var gotFlags SendFlags
	var gotRequestSeq uint64
	var gotReplyCount int
	received := &Received{
		routingID:     NewRoutingID([]byte("peer")),
		spotRID:       NewRoutingID([]byte("spot")),
		requestSeq:    42,
		hasRequestSeq: true,
		parts:         []*Message{replyPart},
	}
	received.reply = func(flags SendFlags, parts []*Message) error {
		gotFlags = flags
		gotRequestSeq = received.RequestSeq()
		gotReplyCount = len(parts)
		return nil
	}

	if !received.HasRoutingID() || !received.HasSpotRID() || !received.HasRequestSeq() {
		t.Fatalf("received helper predicates should reflect stored metadata")
	}
	if !received.IsSinglePart() {
		t.Fatalf("IsSinglePart() = false, want true")
	}
	if got := received.SpotRID(); !got.Equal(NewRoutingID([]byte("spot"))) {
		t.Fatalf("SpotRID() = %v, want %v", got, NewRoutingID([]byte("spot")))
	}
	if err := received.Reply([]*Message{replyPart}); err != nil {
		t.Fatalf("Reply() error = %v", err)
	}
	if gotFlags != SendFlagsNone {
		t.Fatalf("Reply() passed flags %v, want %v", gotFlags, SendFlagsNone)
	}
	if gotRequestSeq != 42 {
		t.Fatalf("RequestSeq() = %d, want 42", gotRequestSeq)
	}
	if gotReplyCount != 1 {
		t.Fatalf("reply part count = %d, want 1", gotReplyCount)
	}
}

func TestReceivedReplyWithFlagsRejectsUnsupportedFlags(t *testing.T) {
	replyPart, err := NewMessage([]byte("reply"))
	if err != nil {
		t.Fatalf("NewMessage() error = %v", err)
	}
	defer replyPart.Close()

	called := false
	received := &Received{
		requestSeq:    42,
		hasRequestSeq: true,
		reply: func(flags SendFlags, parts []*Message) error {
			called = true
			return nil
		},
	}

	err = received.ReplyWithFlags([]*Message{replyPart}, SendFlagsDontWait)
	if err == nil {
		t.Fatalf("ReplyWithFlags() should reject unsupported flags")
	}
	var submitErr *SubmitError
	if !errors.As(err, &submitErr) {
		t.Fatalf("ReplyWithFlags() error type = %T, want *SubmitError", err)
	}
	if submitErr.Result != SubmitNotSupported {
		t.Fatalf("ReplyWithFlags() result = %v, want %v", submitErr.Result, SubmitNotSupported)
	}
	if called {
		t.Fatalf("ReplyWithFlags() should not invoke the reply callback for unsupported flags")
	}
}

func TestReceivedReplyRequiresValidContext(t *testing.T) {
	received := &Received{}
	err := received.Reply(nil)
	if err == nil {
		t.Fatalf("Reply() should fail without a request context")
	}
	var submitErr *SubmitError
	if !errors.As(err, &submitErr) {
		t.Fatalf("Reply() error type = %T, want *SubmitError", err)
	}
	if submitErr.Result != SubmitInvalidArgument {
		t.Fatalf("Reply() result = %v, want %v", submitErr.Result, SubmitInvalidArgument)
	}
}

func TestExportedSpecShapeForMonitorDiscoveryAndErrors(t *testing.T) {
	assertField := func(name string, typ reflect.Type, kind reflect.Kind) {
		field, ok := typ.FieldByName(name)
		if !ok {
			t.Fatalf("%s should expose field %s", typ.Name(), name)
		}
		if kind != reflect.Invalid && field.Type.Kind() != kind {
			t.Fatalf("%s.%s kind = %v, want %v", typ.Name(), name, field.Type.Kind(), kind)
		}
	}
	assertNoField := func(name string, typ reflect.Type) {
		if _, ok := typ.FieldByName(name); ok {
			t.Fatalf("%s should not expose legacy field %s", typ.Name(), name)
		}
	}

	assertField("SourceKind", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("SndPendingMsgs", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint64)
	assertField("RcvPendingMsgs", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint64)
	assertField("AutoHwmObservedCount", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmPlanningCount", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmContextTotalPlanningCount", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmSocketQueueShareBytes", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint64)
	assertField("AutoHwmSocketMessageSlots", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint64)
	assertField("AutoHwmLastRecalcReason", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmSendBlockedRatioPPM", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmScope", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmScopeCount", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint32)
	assertField("AutoHwmAutoBufferBytes", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint64)
	assertField("AutoHwmManualBufferBytes", reflect.TypeOf(MonitorSnapshot{}), reflect.Uint64)
	assertField("AutoHwmDeferredSndHwm", reflect.TypeOf(MonitorSnapshot{}), reflect.Int32)
	assertNoField("AutoHwmControlBudgetBytes", reflect.TypeOf(MonitorSnapshot{}))
	assertNoField("AutoHwmControlActiveConnections", reflect.TypeOf(MonitorSnapshot{}))
	assertNoField("SendPendingMsg", reflect.TypeOf(MonitorSnapshot{}))
	assertNoField("RecvPendingMsg", reflect.TypeOf(MonitorSnapshot{}))

	assertField("LastChangedMs", reflect.TypeOf(SpotNodeStatus{}), reflect.Uint64)
	assertField("DisconnectedSubTargetCount", reflect.TypeOf(SpotNodeStatus{}), reflect.Uint32)
	assertField("DisconnectedRoutedTargetCount", reflect.TypeOf(SpotNodeStatus{}), reflect.Uint32)
	assertField("ConnectedSinceMs", reflect.TypeOf(SpotNodePeerEntry{}), reflect.Uint64)
	assertField("LastReportedMs", reflect.TypeOf(RegistryTopologyEntry{}), reflect.Uint64)
	assertNoField("LastChangedMS", reflect.TypeOf(SpotNodeStatus{}))
	assertNoField("ConnectedSinceMS", reflect.TypeOf(SpotNodePeerEntry{}))
	assertNoField("LastReportedMS", reflect.TypeOf(RegistryTopologyEntry{}))

	assertField("ServiceKind", reflect.TypeOf(RegistryServiceSummaryFilter{}), reflect.Pointer)
	assertField("ServiceRole", reflect.TypeOf(RegistryServiceSummaryFilter{}), reflect.Pointer)
	assertField("ServiceName", reflect.TypeOf(RegistryServiceSummaryFilter{}), reflect.Pointer)
	assertField("RoutingID", reflect.TypeOf(RegistryTopologyFilter{}), reflect.Pointer)
	assertField("PeerEndpoint", reflect.TypeOf(SpotNodePeerFilter{}), reflect.Pointer)
	assertField("Role", reflect.TypeOf(SpotNodeSubjectFilter{}), reflect.Pointer)

	methodNames := []struct {
		typ  reflect.Type
		name string
	}{
		{reflect.TypeOf(&MonitorEvent{}), "HasRoutingID"},
		{reflect.TypeOf(&MemberPeerEntry{}), "HasRoutingID"},
		{reflect.TypeOf(&RegistryTopologyEntry{}), "HasRoutingID"},
		{reflect.TypeOf(&SpotNodeStatus{}), "HasNodeRoutingID"},
	}
	for _, method := range methodNames {
		if _, ok := method.typ.MethodByName(method.name); !ok {
			t.Fatalf("%s should expose %s()", method.typ, method.name)
		}
	}

	errorTypes := []reflect.Type{
		reflect.TypeOf(SubmitError{}),
		reflect.TypeOf(RequestError{}),
		reflect.TypeOf(RecvError{}),
		reflect.TypeOf(HandlerError{}),
		reflect.TypeOf(CloseError{}),
		reflect.TypeOf(BindError{}),
		reflect.TypeOf(ConnectError{}),
		reflect.TypeOf(ConfigError{}),
	}
	for _, typ := range errorTypes {
		if _, ok := typ.FieldByName("InternalErrno"); !ok {
			t.Fatalf("%s should expose InternalErrno field", typ.Name())
		}
	}
}

func TestSubscriptionEventHasRoutingID(t *testing.T) {
	event := SubscriptionEvent{
		routingID:  NewRoutingID([]byte("subscriber")),
		subscribed: true,
		topic:      "topic.alpha",
	}
	if !event.HasRoutingID() {
		t.Fatalf("HasRoutingID() = false, want true")
	}
	if got := event.Topic(); got != "topic.alpha" {
		t.Fatalf("Topic() = %q, want %q", got, "topic.alpha")
	}
	if !event.Subscribed() {
		t.Fatalf("Subscribed() = false, want true")
	}
	if event.HasServiceName() {
		t.Fatalf("SubscriptionEvent zero service name should report false")
	}
}

func TestExportedSpecShapeForSpotServiceBindingTypes(t *testing.T) {
	assertField := func(name string, typ reflect.Type, kind reflect.Kind) {
		field, ok := typ.FieldByName(name)
		if !ok {
			t.Fatalf("%s should expose field %s", typ.Name(), name)
		}
		if kind != reflect.Invalid && field.Type.Kind() != kind {
			t.Fatalf("%s.%s kind = %v, want %v", typ.Name(), name, field.Type.Kind(), kind)
		}
	}

	assertField("serviceName", reflect.TypeOf(TopicMessage{}), reflect.String)
	assertField("serviceName", reflect.TypeOf(SubscriptionEvent{}), reflect.String)
	assertField("Weight", reflect.TypeOf(SpotNodePeerEntry{}), reflect.Uint32)
	assertField("Weight", reflect.TypeOf(MemberPeerEntry{}), reflect.Uint32)
}
