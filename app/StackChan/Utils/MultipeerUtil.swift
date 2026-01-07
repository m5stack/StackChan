/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import MultipeerConnectivity

class MultipeerUtil: NSObject, MCSessionDelegate, MCNearbyServiceAdvertiserDelegate, MCNearbyServiceBrowserDelegate {
    
    static let shared = MultipeerUtil()
    
    private let serviceType = "stackchan-mpc"
    
    private let myPeerID = MCPeerID(displayName: UIDevice.current.name)
    private var session: MCSession!
    
    private var advertiser: MCNearbyServiceAdvertiser!
    private var browser: MCNearbyServiceBrowser!
    
    var appendScanPeer: ((MCPeerID,[String : String]?) -> Void)?
    var removeScanPeer: ((MCPeerID) -> Void)?
    var onMessageReceived: ((Data, MCPeerID) -> Void)?
    
    private override init() {
        super.init()
        session = MCSession(peer: myPeerID, securityIdentity: nil, encryptionPreference: .required)
        session.delegate = self
        
        advertiser = MCNearbyServiceAdvertiser(peer: myPeerID, discoveryInfo: [
            "deviceId": UIDevice.current.identifierForVendor?.uuidString ?? UUID().uuidString
        ], serviceType: serviceType)
        advertiser.delegate = self
        
        browser = MCNearbyServiceBrowser(peer: myPeerID, serviceType: serviceType)
        browser.delegate = self
        
    }
    
    func startAdvertising() {
        advertiser.stopAdvertisingPeer()
        advertiser.startAdvertisingPeer()
        print("Started advertising Multipeer service")
    }
    
    func stopAdvertising() {
        advertiser.stopAdvertisingPeer()
        print("Stopped advertising Multipeer service")
    }
    
    func startBrowsing() {
        browser.stopBrowsingForPeers()
        browser.startBrowsingForPeers()
        print("Started browsing nearby devices")
    }
    
    func stopBrowsing() {
        browser.stopBrowsingForPeers()
        print("Stopped browsing nearby devices")
    }
    
    func sendMessage(_ data: Data, to peer: MCPeerID) {
        if !session.connectedPeers.contains(peer) {
            print("Not connected to \(peer.displayName), attempting to connect...")
            Task {
                await connectAndSend(data: data, to: peer)
            }
            return
        }
        
        do {
            try session.send(data, toPeers: [peer], with: .reliable)
            print("Message sent successfully")
        } catch {
            print("Failed to send message: \(error)")
        }
    }
    
    private func connectAndSend(data: Data, to peer: MCPeerID) async {
        browser.invitePeer(peer, to: session, withContext: nil, timeout: 30)
        for _ in 0..<30 {
            try? await Task.sleep(nanoseconds: 500_000_000) // 0.5 seconds
            if session.connectedPeers.contains(peer) {
                sendMessage(data, to: peer)
                return
            }
        }
        
        print("Connection timed out, unable to send message to \(peer.displayName)")
    }
    
    func browser(_ browser: MCNearbyServiceBrowser, foundPeer peerID: MCPeerID, withDiscoveryInfo info: [String : String]?) {
        print("Discovered peer: " + peerID.displayName)
        appendScanPeer?(peerID, info)
    }
    
    func browser(_ browser: MCNearbyServiceBrowser, lostPeer peerID: MCPeerID) {
        removeScanPeer?(peerID)
    }
    
    func advertiser(_ advertiser: MCNearbyServiceAdvertiser, didReceiveInvitationFromPeer peerID: MCPeerID,
                    withContext context: Data?, invitationHandler: @escaping (Bool, MCSession?) -> Void) {
        invitationHandler(true, session)
    }
    
    func session(_ session: MCSession, peer peerID: MCPeerID, didChange state: MCSessionState) {
        print("Peer \(peerID.displayName) state changed: \(state.rawValue)")
    }
    
    func session(_ session: MCSession, didReceive data: Data, fromPeer peerID: MCPeerID) {
        onMessageReceived?(data, peerID)
    }
    
    
    func session(_ session: MCSession, didReceive stream: InputStream, withName streamName: String, fromPeer peerID: MCPeerID) {
        
    }
    
    func session(_ session: MCSession, didReceiveCertificate certificate: [Any]?, fromPeer peerID: MCPeerID, certificateHandler: @escaping (Bool) -> Void) {
        
    }
    
    func session(_ session: MCSession, didStartReceivingResourceWithName resourceName: String, fromPeer peerID: MCPeerID, with progress: Progress) {
        
    }
    
    func session(_ session: MCSession, didFinishReceivingResourceWithName resourceName: String, fromPeer peerID: MCPeerID, at localURL: URL?, withError error: (any Error)?) {
        
    }
}
