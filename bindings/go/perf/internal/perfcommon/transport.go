package perfcommon

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include
#cgo linux,amd64 LDFLAGS: -L${SRCDIR}/../../../native/linux-x86_64 -lzlink -Wl,-rpath,${SRCDIR}/../../../native/linux-x86_64
#cgo linux,arm64 LDFLAGS: -L${SRCDIR}/../../../native/linux-aarch64 -lzlink -Wl,-rpath,${SRCDIR}/../../../native/linux-aarch64
#cgo darwin,amd64 LDFLAGS: -L${SRCDIR}/../../../native/darwin-x86_64 -lzlink -Wl,-rpath,${SRCDIR}/../../../native/darwin-x86_64
#cgo darwin,arm64 LDFLAGS: -L${SRCDIR}/../../../native/darwin-aarch64 -lzlink -Wl,-rpath,${SRCDIR}/../../../native/darwin-aarch64
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import (
	"errors"
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/binary"
	"encoding/pem"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/url"
	"reflect"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unsafe"
	"syscall"

	"github.com/gorilla/websocket"
)

type StreamConn interface {
	io.ReadWriteCloser
	SetDeadline(time.Time) error
}

type tlsAssetSet struct {
	caPath   string
	certPath string
	keyPath  string
	rootCAs  *x509.CertPool
}

var (
	tlsAssetsOnce sync.Once
	tlsAssets     tlsAssetSet
	tlsAssetsErr  error
)

func EnsureTLSAssets() (tlsAssetSet, error) {
	tlsAssetsOnce.Do(func() {
		tlsAssets, tlsAssetsErr = buildTLSAssets()
	})
	return tlsAssets, tlsAssetsErr
}

func ConfigureTLSServer(socket interface {
	SetTLSServer(string, string, bool) error
}, transport string) error {
	if transport != "tls" && transport != "wss" {
		return nil
	}
	assets, err := EnsureTLSAssets()
	if err != nil {
		return err
	}
	if err := socket.SetTLSServer(assets.certPath, assets.keyPath, false); err == nil {
		return nil
	} else if !isNativeErrCode(err, int(C.EFAULT)) {
		if os.Getenv("PERF_DEBUG") != "" {
			fmt.Fprintf(os.Stderr, "tls server direct failed: cert=%q key=%q err=%v\n", assets.certPath, assets.keyPath, err)
		}
		return err
	}
	handle, err := rawSocketHandle(socket)
	if err != nil {
		return err
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "tls server fallback handle=%p cert=%q key=%q\n", handle, assets.certPath, assets.keyPath)
	}
	if err := setRawStringOption(handle, C.ZLINK_OPT_TLS_CERT, assets.certPath); err != nil {
		return err
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "tls server cert set ok\n")
	}
	return setRawStringOption(handle, C.ZLINK_OPT_TLS_KEY, assets.keyPath)
}

func ConfigureTLSClient(socket interface {
	SetTLSClient(string, string, bool) error
}, transport string) error {
	if transport != "tls" && transport != "wss" {
		return nil
	}
	assets, err := EnsureTLSAssets()
	if err != nil {
		return err
	}
	if err := socket.SetTLSClient(assets.caPath, "localhost", false); err == nil {
		return nil
	} else if !isNativeErrCode(err, int(C.EFAULT)) {
		if os.Getenv("PERF_DEBUG") != "" {
			fmt.Fprintf(os.Stderr, "tls client direct failed: ca=%q hostname=%q err=%v\n", assets.caPath, "localhost", err)
		}
		return err
	}
	handle, err := rawSocketHandle(socket)
	if err != nil {
		return err
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "tls client fallback handle=%p ca=%q hostname=%q\n", handle, assets.caPath, "localhost")
	}
	if err := setRawStringOption(handle, C.ZLINK_OPT_TLS_CA, assets.caPath); err != nil {
		return err
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "tls client ca set ok\n")
	}
	if err := setRawStringOption(handle, C.ZLINK_OPT_TLS_HOSTNAME, "localhost"); err != nil {
		return err
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "tls client hostname set ok\n")
	}
	if err := setRawIntOption(handle, C.ZLINK_OPT_TLS_TRUST_SYSTEM, 0); err != nil {
		return err
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "tls client trust system set ok\n")
	}
	return nil
}

func isNativeErrCode(err error, code int) bool {
	return errors.Is(err, syscall.Errno(code))
}

func rawSocketHandle(value any) (unsafe.Pointer, error) {
	if value == nil {
		return nil, fmt.Errorf("socket handle is nil")
	}
	v := reflect.ValueOf(value)
	for v.IsValid() {
		switch v.Kind() {
		case reflect.Interface, reflect.Ptr:
			if v.IsNil() {
				return nil, fmt.Errorf("socket handle is nil")
			}
			v = v.Elem()
			continue
		case reflect.Struct:
			if handleField := v.FieldByName("handle"); handleField.IsValid() && handleField.Type() == reflect.TypeOf(unsafe.Pointer(nil)) {
				if !handleField.CanAddr() {
					return nil, fmt.Errorf("socket handle is not addressable")
				}
				handle := *(*unsafe.Pointer)(unsafe.Pointer(handleField.UnsafeAddr()))
				if handle == nil {
					return nil, fmt.Errorf("socket handle is nil")
				}
				return handle, nil
			}
			for i := 0; i < v.NumField(); i++ {
				field := v.Field(i)
				if !field.IsValid() {
					continue
				}
				switch field.Kind() {
				case reflect.Interface, reflect.Ptr, reflect.Struct:
					if field.Kind() == reflect.Ptr && field.IsNil() {
						continue
					}
					if field.Kind() == reflect.Interface && field.IsNil() {
						continue
					}
					v = field
					goto next
				}
			}
			return nil, fmt.Errorf("unsupported socket type %T", value)
		default:
			return nil, fmt.Errorf("unsupported socket type %T", value)
		}
	next:
	}
	return nil, fmt.Errorf("unsupported socket type %T", value)
}

func setRawStringOption(handle unsafe.Pointer, option C.zlink_option_t, value string) error {
	if strings.IndexByte(value, 0) >= 0 {
		return fmt.Errorf("string contains null byte")
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	if C.zlink_set_option(handle, option, unsafe.Pointer(cstr), C.size_t(len(value))) != 0 {
		return nativeError()
	}
	return nil
}

func setRawIntOption(handle unsafe.Pointer, option C.zlink_option_t, value int) error {
	raw := C.int(value)
	if C.zlink_set_option(handle, option, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)) != 0 {
		return nativeError()
	}
	return nil
}

func nativeError() error {
	code := int(C.zlink_errno())
	return fmt.Errorf("%s: %w", C.GoString(C.zlink_strerror(C.int(code))), syscall.Errno(code))
}

func buildTLSAssets() (tlsAssetSet, error) {
	dir, err := os.MkdirTemp("", "zlink-perf-tls-*")
	if err != nil {
		return tlsAssetSet{}, err
	}

	caKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return tlsAssetSet{}, err
	}
	caTpl := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject: pkix.Name{
			CommonName:   "ZLink Perf CA",
			Organization: []string{"ZLink"},
			Country:      []string{"US"},
		},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().Add(10 * 365 * 24 * time.Hour),
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		IsCA:                  true,
		BasicConstraintsValid: true,
	}
	caDER, err := x509.CreateCertificate(rand.Reader, caTpl, caTpl, &caKey.PublicKey, caKey)
	if err != nil {
		return tlsAssetSet{}, err
	}

	serverKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return tlsAssetSet{}, err
	}
	serverTpl := &x509.Certificate{
		SerialNumber: big.NewInt(2),
		Subject: pkix.Name{
			CommonName:   "localhost",
			Organization: []string{"ZLink"},
			Country:      []string{"US"},
		},
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().Add(10 * 365 * 24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames:     []string{"localhost"},
		IPAddresses:  []net.IP{net.ParseIP("127.0.0.1")},
	}
	serverDER, err := x509.CreateCertificate(rand.Reader, serverTpl, caTpl, &serverKey.PublicKey, caKey)
	if err != nil {
		return tlsAssetSet{}, err
	}

	caPath := filepath.Join(dir, "ca.pem")
	certPath := filepath.Join(dir, "server_cert.pem")
	keyPath := filepath.Join(dir, "server_key.pem")

	if err := os.WriteFile(caPath, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: caDER}), 0o600); err != nil {
		return tlsAssetSet{}, err
	}
	if err := os.WriteFile(certPath, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: serverDER}), 0o600); err != nil {
		return tlsAssetSet{}, err
	}
	if err := os.WriteFile(keyPath, pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(serverKey)}), 0o600); err != nil {
		return tlsAssetSet{}, err
	}

	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: caDER})) {
		return tlsAssetSet{}, fmt.Errorf("failed to append generated CA certificate")
	}

	return tlsAssetSet{
		caPath:   caPath,
		certPath: certPath,
		keyPath:  keyPath,
		rootCAs:  pool,
	}, nil
}

func DialEndpoint(endpoint string) StreamConn {
	conn, err := openStreamEndpoint(endpoint)
	Must(err)
	return conn
}

func openStreamEndpoint(endpoint string) (StreamConn, error) {
	u, err := url.Parse(endpoint)
	if err != nil || u.Scheme == "" {
		return openTCPStream(endpoint)
	}
	switch strings.ToLower(u.Scheme) {
	case "tcp":
		return openTCPStream(endpoint)
	case "tls":
		return openTLSStream(u.Host)
	case "ws":
		return openWSStream(endpoint, false)
	case "wss":
		return openWSStream(endpoint, true)
	default:
		return openTCPStream(endpoint)
	}
}

func openTCPStream(endpoint string) (StreamConn, error) {
	addr := endpoint
	if idx := strings.Index(addr, "://"); idx >= 0 {
		addr = addr[idx+3:]
	}
	if idx := strings.IndexByte(addr, '?'); idx >= 0 {
		addr = addr[:idx]
	}
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, err
	}
	return &framedNetConn{conn: conn}, nil
}

func openTLSStream(addr string) (StreamConn, error) {
	assets, err := EnsureTLSAssets()
	if err != nil {
		return nil, err
	}
	dialer := &net.Dialer{Timeout: 5 * time.Second}
	config := &tls.Config{
		RootCAs:            assets.rootCAs,
		ServerName:         "127.0.0.1",
		MinVersion:         tls.VersionTLS12,
		InsecureSkipVerify: false,
	}
	conn, err := tls.DialWithDialer(dialer, "tcp", addr, config)
	if err != nil {
		return nil, err
	}
	return &framedNetConn{conn: conn}, nil
}

func openWSStream(endpoint string, secure bool) (StreamConn, error) {
	assets, err := EnsureTLSAssets()
	if err != nil {
		return nil, err
	}
	dialer := websocket.Dialer{
		HandshakeTimeout: 5 * time.Second,
	}
	if secure {
		dialer.TLSClientConfig = &tls.Config{
			RootCAs:            assets.rootCAs,
			ServerName:         "127.0.0.1",
			MinVersion:         tls.VersionTLS12,
			InsecureSkipVerify: false,
		}
	}
	conn, _, err := dialer.Dial(endpoint, nil)
	if err != nil {
		return nil, err
	}
	return &framedWebSocketConn{conn: conn}, nil
}

type framedNetConn struct {
	conn       net.Conn
	pending    []byte
	pendingPos int
}

func (c *framedNetConn) Read(p []byte) (int, error) {
	if c.pendingPos >= len(c.pending) {
		if err := c.fill(); err != nil {
			return 0, err
		}
	}
	if len(c.pending) == 0 {
		return 0, io.EOF
	}
	n := copy(p, c.pending[c.pendingPos:])
	c.pendingPos += n
	return n, nil
}

func (c *framedNetConn) fill() error {
	var header [4]byte
	if _, err := io.ReadFull(c.conn, header[:]); err != nil {
		return err
	}
	size := binary.BigEndian.Uint32(header[:])
	if size == 0 {
		c.pending = c.pending[:0]
		c.pendingPos = 0
		return nil
	}
	buf := make([]byte, int(size))
	if _, err := io.ReadFull(c.conn, buf); err != nil {
		return err
	}
	c.pending = buf
	c.pendingPos = 0
	return nil
}

func (c *framedNetConn) Write(p []byte) (int, error) {
	frame := make([]byte, 4+len(p))
	binary.BigEndian.PutUint32(frame[:4], uint32(len(p)))
	copy(frame[4:], p)
	if err := writeFull(c.conn, frame); err != nil {
		return 0, err
	}
	return len(p), nil
}

func (c *framedNetConn) Close() error {
	return c.conn.Close()
}

func (c *framedNetConn) SetDeadline(t time.Time) error {
	return c.conn.SetDeadline(t)
}

type framedWebSocketConn struct {
	conn       *websocket.Conn
	pending    []byte
	pendingPos int
}

func (c *framedWebSocketConn) Read(p []byte) (int, error) {
	if c.pendingPos >= len(c.pending) {
		if err := c.fill(); err != nil {
			return 0, err
		}
	}
	if len(c.pending) == 0 {
		return 0, io.EOF
	}
	n := copy(p, c.pending[c.pendingPos:])
	c.pendingPos += n
	return n, nil
}

func (c *framedWebSocketConn) fill() error {
	_, data, err := c.conn.ReadMessage()
	if err != nil {
		return err
	}
	if len(data) < 4 {
		return fmt.Errorf("websocket frame too small")
	}
	size := binary.BigEndian.Uint32(data[:4])
	if int(size) > len(data)-4 {
		return fmt.Errorf("websocket frame length mismatch")
	}
	c.pending = append(c.pending[:0], data[4:4+int(size)]...)
	c.pendingPos = 0
	return nil
}

func (c *framedWebSocketConn) Write(p []byte) (int, error) {
	frame := make([]byte, 4+len(p))
	binary.BigEndian.PutUint32(frame[:4], uint32(len(p)))
	copy(frame[4:], p)
	if err := c.conn.WriteMessage(websocket.BinaryMessage, frame); err != nil {
		return 0, err
	}
	return len(p), nil
}

func (c *framedWebSocketConn) Close() error {
	_ = c.conn.WriteMessage(
		websocket.CloseMessage,
		websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""),
	)
	return c.conn.Close()
}

func (c *framedWebSocketConn) SetDeadline(t time.Time) error {
	return c.conn.UnderlyingConn().SetDeadline(t)
}

func writeFull(conn net.Conn, data []byte) error {
	for len(data) > 0 {
		n, err := conn.Write(data)
		if err != nil {
			return err
		}
		data = data[n:]
	}
	return nil
}
