# Project Setup and Running Instructions

## 1. Clone the repository
```bash
git clone https://github.com/m5stack/StackChan
cd StackChan/app
```

## 2. Open the project in Xcode
Open the project in Xcode:

Double‑click the `.xcodeproj` file, or open Xcode → File → Open, then select the project.

1. Select your target device or simulator.

### Connect an iPhone (Optional but Recommended)
- Connect your iPhone to the Mac using a USB cable.
- Unlock the iPhone and tap **Trust This Computer** if prompted.
- In Xcode, select your iPhone as the run destination at the top.

### Enable Developer Mode on iPhone (iOS 16+)
> **Important:** Developer Mode will only appear after the iPhone has been connected to Xcode at least once.  
If you do not see this option, make sure your iPhone is connected to the Mac, unlocked, trusted, and recognized by Xcode.
- On the iPhone, go to **Settings → Privacy & Security → Developer Mode**.
- Turn on Developer Mode and restart the iPhone.
- After restart, confirm enabling Developer Mode.

## 3. Configure Signing & Capabilities
This step allows Xcode to install the app on your iPhone.

1. In Xcode, select the project in the left sidebar.
2. Select the app target.
3. Open the **Signing & Capabilities** tab.
4. Sign in with your Apple ID (Xcode → Settings → Accounts → Add Apple ID).
5. Set **Team** to your Apple ID.
6. Change **Bundle Identifier** to a unique value, for example:
`com.yourname.stackchan`
7. Ensure no red error messages remain.

> **Note:** A free Apple ID is sufficient for testing on your own iPhone.

## 4. Modify network configuration
Before running the app, you need to set the correct server IP:

1. Open the file `Network/Urls.swift`.
2. Find the line defining the base URL, for example:
```swift
// Base URL configured according to the server's IP
static let url = "192.168.51.24:12800/"
```
3. Replace the IP address (`192.168.51.24`) with the IP of the computer where the server is running.
4. Save the file.

## 5. Run the project
Press `Cmd + R` to build and run the app.

> **Note:** The first build may take several minutes as Xcode prepares the environment.

If running on an iPhone for the first time, you may need to trust yourself as a developer:
- On your iPhone, go to **Settings → General → VPN & Device Management → Trust Developer** and trust the developer profile that appears.

The app will now connect to the server at the IP you configured.
