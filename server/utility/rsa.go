/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package utility

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/pem"
	"errors"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gctx"
)

var (
	serverPublicKey  *rsa.PublicKey
	serverPrivateKey *rsa.PrivateKey
	clientPublicKey  *rsa.PublicKey
	clientPrivateKey *rsa.PrivateKey
	initialized      bool
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

	var ctx = gctx.New()

	// Read key strings from configuration file
	serverPublicKeyStr := g.Cfg().MustGet(ctx, "rsa.server.public").String()
	serverPrivateKeyStr := g.Cfg().MustGet(ctx, "rsa.server.private").String()
	clientPublicKeyStr := g.Cfg().MustGet(ctx, "rsa.client.public").String()
	clientPrivateKeyStr := g.Cfg().MustGet(ctx, "rsa.client.private").String()

	// Check if keys are empty
	if serverPublicKeyStr == "" {
		return errors.New("server public key not found in config")
	}
	if serverPrivateKeyStr == "" {
		return errors.New("server private key not found in config")
	}
	if clientPublicKeyStr == "" {
		return errors.New("client public key not found in config")
	}
	if clientPrivateKeyStr == "" {
		return errors.New("client private key not found in config")
	}

	// Parse server public key
	var err error
	serverPublicKey, err = parsePublicKey([]byte(serverPublicKeyStr))
	if err != nil {
		return errors.New("failed to parse server public key: " + err.Error())
	}

	// Parse server private key
	serverPrivateKey, err = parsePrivateKey([]byte(serverPrivateKeyStr))
	if err != nil {
		return errors.New("failed to parse server private key: " + err.Error())
	}

	// Parse client public key
	clientPublicKey, err = parsePublicKey([]byte(clientPublicKeyStr))
	if err != nil {
		return errors.New("failed to parse client public key: " + err.Error())
	}

	// Parse client private key
	clientPrivateKey, err = parsePrivateKey([]byte(clientPrivateKeyStr))
	if err != nil {
		return errors.New("failed to parse client private key: " + err.Error())
	}

	initialized = true
	return nil
}

// parsePublicKey Parse public key in PEM format
func parsePublicKey(pemBytes []byte) (*rsa.PublicKey, error) {
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		return nil, errors.New("failed to parse PEM block containing public key")
	}

	pub, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		// Try parsing PKCS1 format
		pub, err = x509.ParsePKCS1PublicKey(block.Bytes)
		if err != nil {
			return nil, errors.New("failed to parse public key as PKIX or PKCS1: " + err.Error())
		}
	}

	rsaPub, ok := pub.(*rsa.PublicKey)
	if !ok {
		return nil, errors.New("not an RSA public key")
	}

	return rsaPub, nil
}

// parsePrivateKey Parse private key in PEM format
func parsePrivateKey(pemBytes []byte) (*rsa.PrivateKey, error) {
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		return nil, errors.New("failed to parse PEM block containing private key")
	}

	var priv *rsa.PrivateKey
	var err error

	// Try parsing PKCS1 format
	priv, err = x509.ParsePKCS1PrivateKey(block.Bytes)
	if err != nil {
		// Try parsing PKCS8 format
		key, err := x509.ParsePKCS8PrivateKey(block.Bytes)
		if err != nil {
			return nil, errors.New("failed to parse private key as PKCS1 or PKCS8: " + err.Error())
		}
		var ok bool
		priv, ok = key.(*rsa.PrivateKey)
		if !ok {
			return nil, errors.New("not an RSA private key")
		}
	}

	return priv, nil
}

// RSAEncrypt Encrypt using client public key (used when server sends data to client)
func RSAEncrypt(plainText []byte) ([]byte, error) {
	if !initialized {
		return nil, errors.New("RSA keys not initialized")
	}

	// Use OAEP padding, SHA256 hash
	hash := sha256.New()
	ciphertext, err := rsa.EncryptOAEP(hash, rand.Reader, clientPublicKey, plainText, nil)
	if err != nil {
		return nil, err
	}

	return ciphertext, nil
}

// RSADecrypt Decrypt using server private key (used when server receives client data)
func RSADecrypt(cipherText []byte) ([]byte, error) {
	if !initialized {
		return nil, errors.New("RSA keys not initialized")
	}

	// Use OAEP padding, SHA256 hash
	hash := sha256.New()
	plaintext, err := rsa.DecryptOAEP(hash, rand.Reader, serverPrivateKey, cipherText, nil)
	if err != nil {
		return nil, err
	}

	return plaintext, nil
}

// RSAEncryptWithKey Encrypt using specified public key
func RSAEncryptWithKey(plainText []byte, publicKey *rsa.PublicKey) ([]byte, error) {
	if publicKey == nil {
		return nil, errors.New("public key is nil")
	}

	hash := sha256.New()
	ciphertext, err := rsa.EncryptOAEP(hash, rand.Reader, publicKey, plainText, nil)
	if err != nil {
		return nil, err
	}

	return ciphertext, nil
}

// RSADecryptWithKey Decrypt using specified private key
func RSADecryptWithKey(cipherText []byte, privateKey *rsa.PrivateKey) ([]byte, error) {
	if privateKey == nil {
		return nil, errors.New("private key is nil")
	}

	hash := sha256.New()
	plaintext, err := rsa.DecryptOAEP(hash, rand.Reader, privateKey, cipherText, nil)
	if err != nil {
		return nil, err
	}

	return plaintext, nil
}

// GetServerPublicKey Get server public key object
func GetServerPublicKey() (*rsa.PublicKey, error) {
	if !initialized {
		return nil, errors.New("RSA keys not initialized")
	}
	return serverPublicKey, nil
}

// GetServerPrivateKey Get server private key object
func GetServerPrivateKey() (*rsa.PrivateKey, error) {
	if !initialized {
		return nil, errors.New("RSA keys not initialized")
	}
	return serverPrivateKey, nil
}

// GetClientPublicKey Get client public key object
func GetClientPublicKey() (*rsa.PublicKey, error) {
	if !initialized {
		return nil, errors.New("RSA keys not initialized")
	}
	return clientPublicKey, nil
}

// GetClientPrivateKey Get client private key object
func GetClientPrivateKey() (*rsa.PrivateKey, error) {
	if !initialized {
		return nil, errors.New("RSA keys not initialized")
	}
	return clientPrivateKey, nil
}

// GetServerPublicKeyPEM Get server public key PEM format string
func GetServerPublicKeyPEM() (string, error) {
	if !initialized {
		return "", errors.New("RSA keys not initialized")
	}
	return g.Cfg().MustGet(gctx.New(), "rsa.server.public").String(), nil
}

// GetClientPublicKeyPEM Get client public key PEM format string
func GetClientPublicKeyPEM() (string, error) {
	if !initialized {
		return "", errors.New("RSA keys not initialized")
	}
	return g.Cfg().MustGet(gctx.New(), "rsa.client.public").String(), nil
}

// IsInitialized Check if RSA keys are initialized
func IsInitialized() bool {
	return initialized
}

func GenerateFourKeys() {
	// Generate four key pairs and assign to global variables
	//serverPrivateKey, serverPublicKey = generateKeyPair(2048)
	clientPrivateKey, clientPublicKey = generateKeyPair(2048)

	// Print PEM

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
		return "", "", errors.New("failed to encode private key")
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
		return "", "", errors.New("failed to encode public key")
	}
	return string(privPEM), string(pubPEM), nil
}
