/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package utility

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/pem"
	"fmt"

	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gctx"
)

type rsaKeySet struct {
	serverPublic  *rsa.PublicKey
	serverPrivate *rsa.PrivateKey
	clientPublic  *rsa.PublicKey
	clientPrivate *rsa.PrivateKey
}

var (
	rsaKeys     rsaKeySet
	initialized bool
)

func init() {
	if err := InitRSAKeys(); err != nil {
		panic(err)
	}
}

// InitRSAKeys Initialize RSA keys from configuration file
func InitRSAKeys() error {
	if initialized {
		return nil
	}

	ctx := gctx.New()
	keys, err := loadRSAKeySet(ctx)
	if err != nil {
		return err
	}

	rsaKeys = keys
	initialized = true
	return nil
}

func loadRSAKeySet(ctx context.Context) (rsaKeySet, error) {
	var (
		serverPublicPath  = "rsa.server.public"
		serverPrivatePath = "rsa.server.private"
		clientPublicPath  = "rsa.client.public"
		clientPrivatePath = "rsa.client.private"
		serverPublicPEM   = g.Cfg().MustGet(ctx, serverPublicPath).String()
		serverPrivatePEM  = g.Cfg().MustGet(ctx, serverPrivatePath).String()
		clientPublicPEM   = g.Cfg().MustGet(ctx, clientPublicPath).String()
		clientPrivatePEM  = g.Cfg().MustGet(ctx, clientPrivatePath).String()
	)

	keyConfigs := [...]struct {
		path string
		pem  string
	}{
		{path: serverPublicPath, pem: serverPublicPEM},
		{path: serverPrivatePath, pem: serverPrivatePEM},
		{path: clientPublicPath, pem: clientPublicPEM},
		{path: clientPrivatePath, pem: clientPrivatePEM},
	}
	for _, keyConfig := range keyConfigs {
		if keyConfig.pem == "" {
			return rsaKeySet{}, gerror.Newf("RSA key not found in config: %s", keyConfig.path)
		}
	}

	serverPublic, err := parsePublicKey([]byte(serverPublicPEM))
	if err != nil {
		return rsaKeySet{}, gerror.Newf("failed to parse %s: %v", serverPublicPath, err)
	}
	serverPrivate, err := parsePrivateKey([]byte(serverPrivatePEM))
	if err != nil {
		return rsaKeySet{}, gerror.Newf("failed to parse %s: %v", serverPrivatePath, err)
	}
	clientPublic, err := parsePublicKey([]byte(clientPublicPEM))
	if err != nil {
		return rsaKeySet{}, gerror.Newf("failed to parse %s: %v", clientPublicPath, err)
	}
	clientPrivate, err := parsePrivateKey([]byte(clientPrivatePEM))
	if err != nil {
		return rsaKeySet{}, gerror.Newf("failed to parse %s: %v", clientPrivatePath, err)
	}

	if err = validateRSAKeyPair(serverPublic, serverPrivate); err != nil {
		return rsaKeySet{}, gerror.Newf("invalid server key pair: %v", err)
	}
	if err = validateRSAKeyPair(clientPublic, clientPrivate); err != nil {
		return rsaKeySet{}, gerror.Newf("invalid client key pair: %v", err)
	}

	return rsaKeySet{
		serverPublic:  serverPublic,
		serverPrivate: serverPrivate,
		clientPublic:  clientPublic,
		clientPrivate: clientPrivate,
	}, nil
}

func validateRSAKeyPair(publicKey *rsa.PublicKey, privateKey *rsa.PrivateKey) error {
	if publicKey.E != privateKey.PublicKey.E || publicKey.N.Cmp(privateKey.PublicKey.N) != 0 {
		return gerror.New("public and private keys do not match")
	}
	return nil
}

func getRSAKeySet() (*rsaKeySet, error) {
	if !initialized {
		return nil, gerror.New("RSA keys not initialized")
	}
	return &rsaKeys, nil
}

// parsePublicKey Parse public key in PEM format
func parsePublicKey(pemBytes []byte) (*rsa.PublicKey, error) {
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		return nil, gerror.New("failed to parse PEM block containing public key")
	}

	pub, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		// Try parsing PKCS1 format
		pub, err = x509.ParsePKCS1PublicKey(block.Bytes)
		if err != nil {
			return nil, gerror.Newf("failed to parse public key as PKIX or PKCS1: %v", err)
		}
	}

	rsaPub, ok := pub.(*rsa.PublicKey)
	if !ok {
		return nil, gerror.New("not an RSA public key")
	}

	return rsaPub, nil
}

// parsePrivateKey Parse private key in PEM format
func parsePrivateKey(pemBytes []byte) (*rsa.PrivateKey, error) {
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		return nil, gerror.New("failed to parse PEM block containing private key")
	}

	var priv *rsa.PrivateKey
	var err error

	// Try parsing PKCS1 format
	priv, err = x509.ParsePKCS1PrivateKey(block.Bytes)
	if err != nil {
		// Try parsing PKCS8 format
		key, err := x509.ParsePKCS8PrivateKey(block.Bytes)
		if err != nil {
			return nil, gerror.Newf("failed to parse private key as PKCS1 or PKCS8: %v", err)
		}
		var ok bool
		priv, ok = key.(*rsa.PrivateKey)
		if !ok {
			return nil, gerror.New("not an RSA private key")
		}
	}

	return priv, nil
}

// RSAEncrypt Encrypt using client public key (used when server sends data to client)
func RSAEncrypt(plainText []byte) ([]byte, error) {
	keys, err := getRSAKeySet()
	if err != nil {
		return nil, err
	}
	return RSAEncryptWithKey(plainText, keys.clientPublic)
}

// RSADecrypt Decrypt using server private key (used when server receives client data)
func RSADecrypt(cipherText []byte) ([]byte, error) {
	keys, err := getRSAKeySet()
	if err != nil {
		return nil, err
	}
	return RSADecryptWithKey(cipherText, keys.serverPrivate)
}

// RSAEncryptWithKey Encrypt using specified public key
func RSAEncryptWithKey(plainText []byte, publicKey *rsa.PublicKey) ([]byte, error) {
	if publicKey == nil {
		return nil, gerror.New("public key is nil")
	}

	hash := sha256.New()
	ciphertext, err := rsa.EncryptOAEP(hash, rand.Reader, publicKey, plainText, nil)
	if err != nil {
		return nil, gerror.Wrap(err, "RSA encryption failed")
	}

	return ciphertext, nil
}

// RSADecryptWithKey Decrypt using specified private key
func RSADecryptWithKey(cipherText []byte, privateKey *rsa.PrivateKey) ([]byte, error) {
	if privateKey == nil {
		return nil, gerror.New("private key is nil")
	}

	hash := sha256.New()
	plaintext, err := rsa.DecryptOAEP(hash, rand.Reader, privateKey, cipherText, nil)
	if err != nil {
		return nil, gerror.Wrap(err, "RSA decryption failed")
	}

	return plaintext, nil
}

// GetServerPublicKey Get server public key object
func GetServerPublicKey() (*rsa.PublicKey, error) {
	keys, err := getRSAKeySet()
	if err != nil {
		return nil, err
	}
	return keys.serverPublic, nil
}

// GetServerPrivateKey Get server private key object
func GetServerPrivateKey() (*rsa.PrivateKey, error) {
	keys, err := getRSAKeySet()
	if err != nil {
		return nil, err
	}
	return keys.serverPrivate, nil
}

// GetClientPublicKey Get client public key object
func GetClientPublicKey() (*rsa.PublicKey, error) {
	keys, err := getRSAKeySet()
	if err != nil {
		return nil, err
	}
	return keys.clientPublic, nil
}

// GetClientPrivateKey Get client private key object
func GetClientPrivateKey() (*rsa.PrivateKey, error) {
	keys, err := getRSAKeySet()
	if err != nil {
		return nil, err
	}
	return keys.clientPrivate, nil
}

// GetServerPublicKeyPEM Get server public key PEM format string
func GetServerPublicKeyPEM() (string, error) {
	if _, err := getRSAKeySet(); err != nil {
		return "", err
	}
	return g.Cfg().MustGet(gctx.New(), "rsa.server.public").String(), nil
}

// GetClientPublicKeyPEM Get client public key PEM format string
func GetClientPublicKeyPEM() (string, error) {
	if _, err := getRSAKeySet(); err != nil {
		return "", err
	}
	return g.Cfg().MustGet(gctx.New(), "rsa.client.public").String(), nil
}

// IsInitialized Check if RSA keys are initialized
func IsInitialized() bool {
	return initialized
}

func GenerateFourKeys() {
	// Generate four key pairs and assign to global variables
	//rsaKeys.serverPrivate, rsaKeys.serverPublic = generateKeyPair(2048)
	rsaKeys.clientPrivate, rsaKeys.clientPublic = generateKeyPair(2048)

	// Print PEM
	//fmt.Println("=== Server Private Key ===")
	//fmt.Println(keyToPEM(rsaKeys.serverPrivate, true))
	//fmt.Println("=== Server Public Key ===")
	//fmt.Println(keyToPEM(rsaKeys.serverPublic, false))

	fmt.Println("=== Client Private Key ===")
	fmt.Println(keyToPEM(rsaKeys.clientPrivate, true))
	fmt.Println("=== Client Public Key ===")
	fmt.Println(keyToPEM(rsaKeys.clientPublic, false))

	// Mark initialization complete
	initialized = true
}

func generateKeyPair(bits int) (*rsa.PrivateKey, *rsa.PublicKey) {
	privateKey, err := rsa.GenerateKey(rand.Reader, bits)
	if err != nil {
		panic(err)
	}
	return privateKey, &privateKey.PublicKey
}

func keyToPEM(key interface{}, isPrivate bool) string {
	if isPrivate {
		privBytes := x509.MarshalPKCS1PrivateKey(key.(*rsa.PrivateKey))
		return string(pem.EncodeToMemory(&pem.Block{
			Type:  "RSA PRIVATE KEY",
			Bytes: privBytes,
		}))
	} else {
		pubBytes, err := x509.MarshalPKIXPublicKey(key.(*rsa.PublicKey))
		if err != nil {
			panic(err)
		}
		return string(pem.EncodeToMemory(&pem.Block{
			Type:  "PUBLIC KEY",
			Bytes: pubBytes,
		}))
	}
}

// GenerateROSAKeyPair Quickly generate single RSA key pair (general method)
// bits: Key length, recommended 2048/4096
// Return: private key PEM string, public key PEM string, error message
func GenerateROSAKeyPair(bits int) (privateKeyPEM, publicKeyPEM string, err error) {
	privateKey, err := rsa.GenerateKey(rand.Reader, bits)
	if err != nil {
		return "", "", err
	}
	privBytes := x509.MarshalPKCS1PrivateKey(privateKey)
	privPEM := pem.EncodeToMemory(&pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: privBytes,
	})
	if privPEM == nil {
		return "", "", gerror.New("failed to encode private key")
	}
	pubBytes, err := x509.MarshalPKIXPublicKey(&privateKey.PublicKey)
	if err != nil {
		return "", "", err
	}
	pubPEM := pem.EncodeToMemory(&pem.Block{
		Type:  "PUBLIC KEY",
		Bytes: pubBytes,
	})
	if pubPEM == nil {
		return "", "", gerror.New("failed to encode public key")
	}
	return string(privPEM), string(pubPEM), nil
}
