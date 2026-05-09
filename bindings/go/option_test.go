package zlink_test

import (
	"testing"
	"time"

	"zlink.systems/zlink"
)

func TestCommonTypedOptions(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()
	opts := ctx.Options()

	socket, _ := ctx.PairSocket()
	defer socket.Close()

	if err := socket.SetSendHWM(1000); err != nil {
		t.Fatalf("SetSendHWM() error = %v", err)
	}
	if got, err := socket.SendHWM(); err != nil || got != 1000 {
		t.Fatalf("SendHWM() = (%d, %v), want (1000, nil)", got, err)
	}
	if err := socket.SetRecvHWM(2000); err != nil {
		t.Fatalf("SetRecvHWM() error = %v", err)
	}
	if got, err := socket.RecvHWM(); err != nil || got != 2000 {
		t.Fatalf("RecvHWM() = (%d, %v), want (2000, nil)", got, err)
	}
	if err := socket.SetLinger(50 * time.Millisecond); err != nil {
		t.Fatalf("SetLinger() error = %v", err)
	}
	if err := socket.SetTCPKeepalive(true); err != nil {
		t.Fatalf("SetTCPKeepalive() error = %v", err)
	}
	if err := socket.SetTCPNoDelay(true); err != nil {
		t.Fatalf("SetTCPNoDelay() error = %v", err)
	}
	if err := socket.SetIPv6(false); err != nil {
		t.Fatalf("SetIPv6() error = %v", err)
	}
	if err := opts.SetMaxSockets(1024); err != nil {
		t.Fatalf("SetMaxSockets() error = %v", err)
	}
	if got, err := opts.MaxSockets(); err != nil || got != 1024 {
		t.Fatalf("MaxSockets() = (%d, %v), want (1024, nil)", got, err)
	}
	if err := opts.SetBlocky(false); err != nil {
		t.Fatalf("SetBlocky() error = %v", err)
	}
	if got, err := opts.Blocky(); err != nil || got {
		t.Fatalf("Blocky() = (%v, %v), want (false, nil)", got, err)
	}
	if err := opts.SetAutoHwmProfile(zlink.AutoHwmProfileCompact); err != nil {
		t.Fatalf("SetAutoHwmProfile() error = %v", err)
	}
	if got, err := opts.AutoHwmProfile(); err != nil || got != zlink.AutoHwmProfileCompact {
		t.Fatalf("AutoHwmProfile() = (%v, %v), want (compact, nil)", got, err)
	}
}

func TestSocketSpecificOptions(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	router, _ := ctx.RouterSocket()
	defer router.Close()
	if err := router.SetMandatory(true); err != nil {
		t.Fatalf("SetMandatory() error = %v", err)
	}
	if err := router.SetRidDuplicatePolicy(int(zlink.RidDuplicateHandover)); err != nil {
		t.Fatalf("SetRidDuplicatePolicy() error = %v", err)
	}
	if err := router.SetProbe(true); err != nil {
		t.Fatalf("SetProbe() error = %v", err)
	}
	rid := zlink.NewRoutingID([]byte("router-typed-options"))
	if err := router.SetRoutingID(rid); err != nil {
		t.Fatalf("SetRoutingID() error = %v", err)
	}
	if got, err := router.RoutingID(); err != nil || !got.Equal(rid) {
		t.Fatalf("RoutingID() = (%v, %v), want (%v, nil)", got, err, rid)
	}
	if err := router.SetRequestTimeout(123 * time.Millisecond); err != nil {
		t.Fatalf("SetRequestTimeout() error = %v", err)
	}
	if got, err := router.RequestTimeout(); err != nil || got != 123*time.Millisecond {
		t.Fatalf("RequestTimeout() = (%v, %v), want (123ms, nil)", got, err)
	}

	dealer, _ := ctx.DealerSocket()
	defer dealer.Close()
	if err := dealer.SetProbe(true); err != nil {
		t.Fatalf("Dealer.SetProbe() error = %v", err)
	}
	if err := dealer.SetRequestTimeout(123 * time.Millisecond); err != nil {
		t.Fatalf("Dealer.SetRequestTimeout() error = %v", err)
	}

	xpub, _ := ctx.XPubSocket()
	defer xpub.Close()
	if err := xpub.SetVerbose(true); err != nil {
		t.Fatalf("XPub.SetVerbose() error = %v", err)
	}
	if err := xpub.SetVerboser(true); err != nil {
		t.Fatalf("XPub.SetVerboser() error = %v", err)
	}
	if err := xpub.SetManual(true); err != nil {
		t.Fatalf("XPub.SetManual() error = %v", err)
	}
	if err := xpub.SetNoDrop(true); err != nil {
		t.Fatalf("XPub.SetNoDrop() error = %v", err)
	}
	pubOpts := xpub.PubOptions()
	if err := pubOpts.SetManualLastValue(true); err != nil {
		t.Fatalf("PubOptions.SetManualLastValue() error = %v", err)
	}
	welcome, err := zlink.NewMessage([]byte("welcome"))
	if err != nil {
		t.Fatalf("NewMessage() error = %v", err)
	}
	defer welcome.Close()
	if err := pubOpts.SetWelcomeMessage(welcome); err != nil {
		t.Fatalf("PubOptions.SetWelcomeMessage() error = %v", err)
	}

	stream, _ := ctx.StreamSocket()
	defer stream.Close()
	if err := stream.SetNotify(true); err != nil {
		t.Fatalf("SetNotify() error = %v", err)
	}
	if got, err := stream.Notify(); err != nil || !got {
		t.Fatalf("Notify() = (%v, %v), want (true, nil)", got, err)
	}

	sub, _ := ctx.SubSocket()
	defer sub.Close()
	if err := sub.SetSubscription("alpha"); err != nil {
		t.Fatalf("SetSubscription() error = %v", err)
	}
	filter, isPattern, err := sub.SubscriptionAt(0)
	if err != nil {
		t.Fatalf("SubscriptionAt() error = %v", err)
	}
	if filter != "alpha" {
		t.Fatalf("SubscriptionAt() filter = %q, want %q", filter, "alpha")
	}
	if isPattern {
		t.Fatalf("SubscriptionAt() isPattern = true, want false")
	}
	if got, err := sub.TopicsCount(); err != nil || got < 1 {
		t.Fatalf("TopicsCount() = (%d, %v), want (>=1, nil)", got, err)
	}
}
