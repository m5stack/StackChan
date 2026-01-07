/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI

struct DanceData : Codable,Identifiable {
    var leftEye: ExpressionItem // Left eye, default weight = 100
    var rightEye: ExpressionItem // Right eye, default weight = 100
    var mouth: ExpressionItem // Mouth, default weight = 0
    var yawServo: MotionDataItem // Yaw rotation, angle range (-1280 ~ 1280), default 0
    var pitchServo: MotionDataItem  // Pitch movement, angle range (0 ~ 900), default 0
    var durationMs: Int // Duration in milliseconds, default 1000
    var id: String = UUID().uuidString
    
    enum CodingKeys: String, CodingKey {
        case leftEye, rightEye, mouth, yawServo, pitchServo, durationMs
    }
    
    func copy() -> DanceData {
        DanceData(
            leftEye: self.leftEye.copy(),
            rightEye: self.rightEye.copy(),
            mouth: self.mouth.copy(),
            yawServo: self.yawServo.copy(),
            pitchServo: self.pitchServo.copy(),
            durationMs: self.durationMs,
            id: UUID().uuidString
        )
    }
}

///
struct Dance : View {
    
    @State var danceList: [DanceData] = []
    
    @State var modelDanceList: [DanceData] = []
    
    @State private var selectedDance: Int = 0
    
    @EnvironmentObject var appState: AppState
    
    @State var showAddDance : Bool = false
    
    @State var isRun: Bool = false
    
    @State var editDanceData = DanceData(leftEye: ExpressionItem(), rightEye: ExpressionItem(), mouth: ExpressionItem(), yawServo: MotionDataItem(), pitchServo: MotionDataItem(), durationMs: 1000)
    
    @State var editDanceDataIndex: Int? = nil
    
    @State private var danceTimer: Timer? = nil
    
    var body: some View {
        List {
            ForEach(Array(danceList.enumerated()), id: \.element.id) { index,item in
                danceItemView(index: index)
                    .swipeActions(edge: .trailing) {
                        Button(role: .destructive) {
                            danceList.remove(at: index)
                            saveDance()
                        } label: {
                            Label("Delete", systemImage: "trash")
                        }
                    }
            }
            .onMove { source, destination in
                danceList.move(fromOffsets: source, toOffset: destination)
                saveDance()
            }
        }
        .listStyle(.insetGrouped)
        .refreshable {
            getDanceList()
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    isRun.toggle()
                    if isRun {
                        startDance()
                    } else {
                        stopDance()
                    }
                } label: {
                    Label {
                        Text(isRun ? "Stop" : "Run")
                    } icon: {
                        Image(systemName: isRun ? "stop.fill" : "play.fill")
                    }
                }
            }
            if #available(iOS 26.0, *) {
                ToolbarSpacer(.fixed, placement: .primaryAction)
            }
            
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button {
                        selectedDance = 0
                    } label: {
                        if selectedDance == 0 {
                            Label("Dance One", systemImage: "checkmark")
                        } else {
                            Text("Dance One")
                        }
                    }
                    
                    Button {
                        selectedDance = 1
                    } label: {
                        if selectedDance == 1 {
                            Label("Dance Two", systemImage: "checkmark")
                        } else {
                            Text("Dance Two")
                        }
                    }
                    
                    Button {
                        selectedDance = 2
                    } label: {
                        if selectedDance == 2 {
                            Label("Dance Three", systemImage: "checkmark")
                        } else {
                            Text("Dance Three")
                        }
                    }
                } label: {
                    Label {
                        Text("Dance")
                    } icon: {
                        Image(systemName: "figure.dance")
                    }
                }
            }
            
            if #available(iOS 26.0, *) {
                ToolbarSpacer(.fixed, placement: .primaryAction)
            }
            
            ToolbarItem(placement: .primaryAction) {
                Button {
                    editDanceData = DanceData(leftEye: ExpressionItem(weight: 100), rightEye: ExpressionItem(weight: 100), mouth: ExpressionItem(), yawServo: MotionDataItem(), pitchServo: MotionDataItem(), durationMs: 1000)
                    editDanceDataIndex = nil
                    danceList.append(editDanceData)
                    saveDance()
                } label: {
                    Label {
                        Text("Add Dance")
                    } icon: {
                        Image(systemName: "plus")
                    }
                }
            }
        }
        .toolbar(.hidden, for: .tabBar)
        .navigationTitle(danceTitle())
        .onAppear {
            getDanceList()
        }
        .onDisappear{
            isRun = false
            stopDance()
        }
    }
    
    private func startDance() {
        var duration = 1000
        let jsonString = danceList.toJsonString()
        for i in danceList {
            duration = duration + i.durationMs
        }
        appState.sendWebSocketMessage(.dance, jsonString.toData())
        let interval = max(Double(duration) / 1000.0, 0.1)
        danceTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { timer in
            appState.sendWebSocketMessage(.dance, jsonString.toData())
        }
    }
    
    /// Stop the dance timer
    private func stopDance() {
        danceTimer?.invalidate()
        danceTimer = nil
    }
    
    private func danceTitle() -> String {
        switch selectedDance {
        case 0: return "Dance One"
        case 1: return "Dance Two"
        case 2: return "Dance Three"
        default: return "Dance"
        }
    }
    
    private func sendDanceData(data: DanceData) {
        if !appState.deviceMac.isEmpty {
            let motionData = MotionData(pitchServo: data.pitchServo, yawServo: data.yawServo)
            let jsonString = appState.deviceMac + motionData.toJsonString()
            let data = jsonString.toData()
            appState.sendWebSocketMessage(.controlMotion, data)
        }
    }
    
    private func danceItemView(index: Int) -> some View {
        HStack {
            VStack {
                if modelDanceList.count > index {
                    StackChanRobot(data: modelDanceList[index])
                        .frame(width: 80,height: 80)
                }
                Spacer()
                Button {
                    let currentData = danceList[index].copy()
                    if danceList.indices.contains(index + 1) {
                        danceList.insert(currentData, at: index + 1)
                    } else {
                        danceList.append(currentData)
                    }
                    saveDance()
                } label: {
                    Label("Copy", systemImage: "doc.on.doc")
                }
            }
            VStack(alignment: .leading) {
                Text("Left-Right")
                    .frame(width: 80,alignment: .leading)
                HStack {
                    Slider(
                        value: Binding(
                            get: { Double(danceList[index].yawServo.angle) },
                            set: { danceList[index].yawServo.angle = Int($0) }
                        ),
                        in: -1280...1280,
                        step: 10,
                        onEditingChanged: { editing in
                            if !editing {
                                saveDance()
                                sendDanceData(data: danceList[index])
                            }
                        }
                    )
                    .frame(maxWidth: .infinity)
                    Text(String(danceList[index].yawServo.angle))
                        .frame(width: 50,alignment: .trailing)
                }
                Text("Up-down")
                    .frame(width: 80,alignment: .leading)
                HStack {
                    Slider(
                        value: Binding(
                            get: { Double(danceList[index].pitchServo.angle) },
                            set: { danceList[index].pitchServo.angle = Int($0) }
                        ),
                        in: 0...900,
                        step: 10,
                        onEditingChanged: { editing in
                            if !editing {
                                saveDance()
                                sendDanceData(data: danceList[index])
                            }
                        }
                    )
                    .frame(maxWidth: .infinity)
                    Text(String(danceList[index].pitchServo.angle))
                        .frame(width: 50,alignment: .trailing)
                }
                Text("Duration")
                    .frame(width: 80,alignment: .leading)
                HStack {
                    Slider(
                        value: Binding(
                            get: { Double(danceList[index].durationMs) },
                            set: { danceList[index].durationMs = Int($0) }
                        ),
                        in: 0...3000,
                        step: 10,
                        onEditingChanged: { editing in
                            if !editing {
                                saveDance()
                            }
                        }
                    )
                    .frame(maxWidth: .infinity)
                    Text(String(danceList[index].durationMs))
                        .frame(width: 50,alignment: .trailing)
                }
            }
        }
    }
    
    private func getDanceList() {
        let map = [
            ValueConstant.mac: appState.deviceMac
        ]
        Networking.shared.get(pathUrl: Urls.dance,parameters: map) { result in
            switch result {
            case .success(let success):
                do {
                    let response = try Response<[String:[DanceData]]>.decode(from: success)
                    if response.isSuccess, let map = response.data {
                        if selectedDance == 0 {
                            danceList = map["0"] ?? []
                        } else if selectedDance == 1 {
                            danceList = map["1"] ?? []
                        } else if selectedDance == 2 {
                            danceList = map["2"] ?? []
                        }
                        modelDanceList = danceList
                    }
                } catch {
                    print("Failed to parse response data")
                }
            case .failure(let failure):
                print("Request failed:", failure)
            }
        }
    }
    
    private func saveDance() {
        if let dict = danceList.toListDictionary() {
            let map: [String: Any] = [
                ValueConstant.mac: appState.deviceMac,
                ValueConstant.list: dict,
                ValueConstant.index: selectedDance
            ]
            Networking.shared.post(pathUrl: Urls.dance,parameters: map) { result in
                switch result {
                case .success(let success):
                    do {
                        let response = try Response<String>.decode(from: success)
                        if response.isSuccess, let data = response.data {
                            modelDanceList = danceList
                            print(data)
                        }
                    } catch {
                        print("Failed to parse response data")
                    }
                case .failure(let failure):
                    print("Request failed:", failure)
                }
            }
        }
    }
}

struct AddAvatarMotion : View {
    
    @Binding var isPresented: Bool
    
    @Binding var editDanceDataIndex: Int?
    
    @State private var selectedItem: ControlItem = .avatar
    
    @EnvironmentObject var appState: AppState
    
    @Binding var danceData: DanceData
    
    let onCallBack : ((DanceData) -> Void)?
    
    enum ControlItem: String,CaseIterable, Identifiable {
        case avatar = "Avatar"
        case motion = "Motion"
        var id: String { rawValue }
    }
    
    var body: some View {
        NavigationStack {
            VStack {
                HStack {
                    Spacer()
                    StackChanRobot(data: danceData,allowsCameraControl: false)
                        .frame(width: 300,height: 300)
                    Spacer()
                }
                HStack {
                    Text("duration")
                        .frame(width: 100,alignment: .leading)
                    Slider(
                        value: Binding(
                            get: { Double(danceData.durationMs) },
                            set: { danceData.durationMs = Int($0) }
                        ),
                        in: 0...3000
                    )
                    .frame(maxWidth: .infinity)
                    Text(String(danceData.durationMs))
                        .frame(width: 50,alignment: .trailing)
                }
                HStack {
                    Picker("Select", selection: $selectedItem) {
                        ForEach(ControlItem.allCases) { item in
                            Text(item.rawValue)
                                .tag(item)
                        }
                    }
                    .pickerStyle(.segmented)
                    Button {
                        withAnimation {
                            danceData = DanceData(leftEye: ExpressionItem(weight: 100), rightEye: ExpressionItem(weight: 100), mouth: ExpressionItem(), yawServo: MotionDataItem(), pitchServo: MotionDataItem(), durationMs: 1000)
                        }
                        saveData()
                    } label: {
                        Image(systemName: "arrow.counterclockwise")
                    }
                    .glassButtonStyle()
                }
                
                if selectedItem == .avatar {
                    List {
                        Section("Left Eye") {
                            HStack {
                                Text("x")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.leftEye.x) },
                                        set: { danceData.leftEye.x = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.leftEye.x))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("y")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.leftEye.y) },
                                        set: { danceData.leftEye.y = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.leftEye.y))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("rotation")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.leftEye.rotation) },
                                        set: { danceData.leftEye.rotation = Int($0) }
                                    ),
                                    in: -1800...1800,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.leftEye.rotation))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("weight")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.leftEye.weight) },
                                        set: { danceData.leftEye.weight = Int($0) }
                                    ),
                                    in: 0...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.leftEye.weight))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("size")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.leftEye.size) },
                                        set: { danceData.leftEye.size = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.leftEye.size))
                                    .frame(width: 50,alignment: .trailing)
                            }
                        }
                        .listRowBackground(Color.clear)
                        
                        Section("Right Eye") {
                            HStack {
                                Text("x")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.rightEye.x) },
                                        set: { danceData.rightEye.x = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.rightEye.x))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("y")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.rightEye.y) },
                                        set: { danceData.rightEye.y = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.rightEye.y))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("rotation")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.rightEye.rotation) },
                                        set: { danceData.rightEye.rotation = Int($0) }
                                    ),
                                    in: -1800...1800,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.rightEye.rotation))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("weight")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.rightEye.weight) },
                                        set: { danceData.rightEye.weight = Int($0) }
                                    ),
                                    in: 0...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.rightEye.weight))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("size")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.rightEye.size) },
                                        set: { danceData.rightEye.size = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.rightEye.size))
                                    .frame(width: 50,alignment: .trailing)
                            }
                        }
                        .listRowBackground(Color.clear)
                        
                        Section("Mouth") {
                            HStack {
                                Text("x")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.mouth.x) },
                                        set: { danceData.mouth.x = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.mouth.x))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("y")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.mouth.y) },
                                        set: { danceData.mouth.y = Int($0) }
                                    ),
                                    in: -100...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.mouth.y))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("rotation")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.mouth.rotation) },
                                        set: { danceData.mouth.rotation = Int($0) }
                                    ),
                                    in: -1800...1800,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.mouth.rotation))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("weight")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.mouth.weight) },
                                        set: { danceData.mouth.weight = Int($0) }
                                    ),
                                    in: 0...100,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.mouth.weight))
                                    .frame(width: 50,alignment: .trailing)
                            }
                        }
                        .listRowBackground(Color.clear)
                    }
                    .listStyle(.grouped)
                    .scrollContentBackground(.hidden)
                    .background(.clear)
                } else if selectedItem == .motion {
                    List {
                        Section("Yaw Servo") {
                            HStack {
                                Text("angle")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.yawServo.angle) },
                                        set: {
                                            danceData.yawServo.angle = Int($0)
                                        }
                                    ),
                                    in: -1280...1280,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.yawServo.angle))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("speed")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.yawServo.speed) },
                                        set: { danceData.yawServo.speed = Int($0) }
                                    ),
                                    in: 0...1000,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.yawServo.speed))
                                    .frame(width: 50,alignment: .trailing)
                            }
                        }
                        .listRowBackground(Color.clear)
                        Section("Pitch Servo") {
                            HStack {
                                Text("angle")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.pitchServo.angle) },
                                        set: { danceData.pitchServo.angle = Int($0) }
                                    ),
                                    in: 0...900,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.pitchServo.angle))
                                    .frame(width: 50,alignment: .trailing)
                            }
                            HStack {
                                Text("speed")
                                    .frame(width: 60,alignment: .leading)
                                Slider(
                                    value: Binding(
                                        get: { Double(danceData.pitchServo.speed) },
                                        set: { danceData.pitchServo.speed = Int($0) }
                                    ),
                                    in: 0...1000,
                                    onEditingChanged: { editing in
                                        if !editing {
                                            saveData()
                                        }
                                    }
                                )
                                .frame(maxWidth: .infinity)
                                Text(String(danceData.pitchServo.speed))
                                    .frame(width: 50,alignment: .trailing)
                            }
                        }
                        .listRowBackground(Color.clear)
                    }
                    .listStyle(.grouped)
                    .scrollContentBackground(.hidden)
                    .background(.clear)
                }
                Spacer()
            }
            .padding()
            .ignoresSafeArea(edges: .bottom)
            .navigationTitle(editDanceDataIndex == nil ? "Add Dance" : "Edit Dance")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar{
                ToolbarItem(placement: .confirmationAction) {
                    Button {
                        self.onCallBack?(danceData)
                        isPresented = false
                    } label: {
                        Image(systemName: "checkmark")
                    }
                }
                ToolbarItem(placement: .cancellationAction) {
                    Button {
                        isPresented = false
                    } label: {
                        Image(systemName: "xmark")
                    }
                }
            }
        }
    }
    
    private func saveData() {
        if !appState.deviceMac.isEmpty {
            
        }
    }
}
