# StackChan App

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Flutter](https://img.shields.io/badge/Flutter-3.0+-blue.svg)](https://flutter.dev/)
[![Dart](https://img.shields.io/badge/Dart-3.0+-blue.svg)](https://dart.dev/)

A powerful Flutter application for controlling and interacting with the StackChan AI robot companion. Features include
Bluetooth connectivity, AI conversation capabilities, facial expression rendering, and dance choreography.

## Features

- 🤖 **Bluetooth Device Management** - Connect and control StackChan robots via BLE
- 💬 **AI Conversation** - Natural language interaction powered by XiaoZhi AI
- 🎭 **Facial Expression Rendering** - Real-time 3D face animation using Three.js
- 🎵 **Music & Dance** - Create and play dance choreographies with music
- 📷 **Camera Integration** - AR features and face detection
- 🔐 **Secure Communication** - RSA encryption for data transmission

## System Requirements

- **Flutter SDK**: 3.0+
- **Dart SDK**: 3.0+
- **iOS**: 14.0+ (for iOS deployment)
- **Android**: API 21+ (for Android deployment)
- **macOS**: 11.0+ (for macOS deployment)
## Installation

### 1. Install Flutter

Follow the official Flutter installation guide for your operating system:

#### macOS

```bash
# Download Flutter SDK
git clone https://github.com/flutter/flutter.git -b stable
export PATH="$PATH:`pwd`/flutter/bin"

# Verify installation
flutter doctor
```

#### Windows

```bash
# Download Flutter SDK from https://flutter.dev/docs/get-started/install/windows
# Extract and add to PATH

# Verify installation
flutter doctor
```

#### Linux

```bash
# Download Flutter SDK
git clone https://github.com/flutter/flutter.git -b stable
export PATH="$PATH:`pwd`/flutter/bin"

# Install dependencies
sudo apt-get install clang cmake ninja-build pkg-config libgtk-3-dev liblzma-dev

# Verify installation
flutter doctor
```

### 2. Set Up Project

```bash
# Clone the repository
git clone <repository-url>
cd StackChan

# Install dependencies
flutter pub get
```

### 3. Configure Backend Server

The application requires a backend server for full functionality. Configure the server endpoints before building:

#### Option A: Using Environment Configuration (Recommended)

Create a `.env` file in the project root or modify the configuration directly in code.

#### Option B: Direct Code Configuration

Modify `lib/network/urls.dart` to set your backend server URL:

```dart
// lib/network/urls.dart
class Urls {
  // Update this to your backend server address
  static const String url = "your-backend-server:port/";

// ... rest of the configuration
}
```

#### Option C: Configure Value Constants

Update `lib/util/value_constant.dart` for encryption keys and other constants:

```dart
// lib/util/value_constant.dart
class ValueConstant {
  // Server RSA Public Key for encryption
  static const String serverPublicKey = """
-----BEGIN PUBLIC KEY-----
YOUR_SERVER_PUBLIC_KEY_HERE
-----END PUBLIC KEY-----
""";

  // Client RSA Private Key for decryption
  static const String clientPrivateKey = """
-----BEGIN RSA PRIVATE KEY-----
YOUR_CLIENT_PRIVATE_KEY_HERE
-----END RSA PRIVATE KEY-----
""";
}
```

**Important**: For production deployments, use environment variables or secure key management instead of hardcoding
keys.

## Building the Application

### iOS

```bash
# Install CocoaPods dependencies
cd ios
pod install
cd ..

# Run on iOS simulator
flutter run -d ios

# Build for release (iOS device)
flutter build ios --release
```

### Android

```bash
# Run on Android emulator or connected device
flutter run -d android

# Build APK for release
flutter build apk --release

# Build App Bundle for Google Play
flutter build appbundle --release
```

### Android Release Signing (JKS)

For release builds (`apk --release` / `appbundle --release`), configure a keystore instead of hardcoding passwords in
`build.gradle.kts`.

#### 1. Generate a JKS file

```bash
keytool -genkeypair -v \
  -keystore android/app/release.jks \
  -alias release \
  -keyalg RSA -keysize 2048 -validity 10000
```

#### 2. Create `android/key.properties`

```properties
storePassword=YOUR_STORE_PASSWORD
keyPassword=YOUR_KEY_PASSWORD
keyAlias=release
storeFile=../app/release.jks
```

> `android/.gitignore` already ignores `key.properties` and `*.jks`. Keep these files private.

#### 3. Load the properties in `android/app/build.gradle.kts`

Use Kotlin DSL style configuration:

```kotlin
import java.util.Properties

val keystoreProperties = Properties()
val keystorePropertiesFile = rootProject.file("key.properties")
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(keystorePropertiesFile.inputStream())
}

android {
    signingConfigs {
        create("release") {
            if (keystorePropertiesFile.exists()) {
                storeFile = file(keystoreProperties["storeFile"] as String)
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("release")
        }
    }
}
```

#### 4. Build release artifacts

```bash
flutter build apk --release
flutter build appbundle --release
```

#### 5. CI/CD recommendation

In CI, inject signing values through environment variables or secure secrets storage. Do not commit keystore passwords or
private keys to the repository.

## Project Structure

```
lib/
├── main.dart                    # Application entry point
├── app_state.dart               # Global state management
├── model/                       # Data models
│   ├── XiaoZhi/                 # AI service models
│   ├── blue_device_info.dart    # Bluetooth device models
│   ├── dance_list.dart          # Dance choreography models
│   └── ...
├── network/                     # Network layer
│   ├── http.dart                # HTTP client with interceptors
│   ├── urls.dart                # API endpoint configurations
│   └── web_socket_util.dart     # WebSocket management
├── util/                        # Utilities
│   ├── value_constant.dart      # App constants and keys
│   ├── rsa_util.dart            # RSA encryption/decryption
│   ├── blue_util.dart           # Bluetooth utilities
│   ├── music_util.dart          # Music and audio processing
│   └── ...
└── view/                        # UI layer
    ├── home/                    # Home screens
    ├── popup/                   # Modal screens
    └── util/                    # UI components and widgets
```

## Backend API Integration

The application integrates with two main backend services:

### 1. StackChan Backend (`lib/network/urls.dart`)

- Device registration and management
- Dance choreography storage
- User authentication
- File upload and media management

### 2. XiaoZhi AI Service (`lib/util/XiaoZhi_util.dart`)

- AI conversation and chat functionality
- Agent management and configuration
- TTS (Text-to-Speech) voice selection
- License and activation management

**Base URL Configuration:**

- StackChan Backend: `http://<server-ip>:<port>/stackChan/`
- XiaoZhi AI: `https://XiaoZhi.me/`

## Development

### Code Style

This project follows the official Dart and Flutter style guidelines:

- Use `camelCase` for variables and functions
- Use `PascalCase` for classes and types
- Use `snake_case` for JSON keys (API communication)
- Document public APIs with doc comments

### Running Tests

```bash
# Run all tests
flutter test

# Run specific test file
flutter test test/widget_test.dart

# Run with coverage
flutter test --coverage
```

### Linting

```bash
# Run static analysis
flutter analyze
```

## Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for more details.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Troubleshooting

### Common Issues

**1. Flutter doctor reports missing dependencies**

- Follow the instructions provided by `flutter doctor` to install missing components
- For iOS: Ensure Xcode is installed and command line tools are selected
- For Android: Ensure Android Studio and SDK are properly configured

**2. Bluetooth not working**

- Ensure Bluetooth permissions are granted
- For iOS: Check `NSBluetoothAlwaysUsageDescription` in Info.plist
- For Android: Check `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` permissions

**3. Backend connection fails**

- Verify server URL is correct in `lib/network/urls.dart`
- Check network connectivity
- Verify backend server is running and accessible
- Check SSL certificates for HTTPS connections

**4. Build fails on iOS**

```bash
cd ios
rm -rf Pods Podfile.lock
pod install --repo-update
cd ..
flutter clean
flutter pub get
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- M5Stack Technology CO LTD for the StackChan hardware
- Flutter team for the amazing framework
- Three.js for 3D rendering capabilities
- All contributors and open source libraries used in this project

## Support

For support, please:

1. Check the [Issues](../../issues) page for known problems
2. Create a new issue if your problem isn't already listed
3. For security issues, please contact security@m5stack.com directly

---

**Note**: This application requires compatible StackChan hardware and backend services for full functionality.