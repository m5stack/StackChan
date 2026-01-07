/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

import SwiftUI
import PhotosUI

struct Moments : View {
    
    @State private var posts: [Post] = []
    
    @State private var showAddMoment: Bool = false
    
    @EnvironmentObject var appState: AppState
    
    @State private var page = 1
    private let pageSize = 10
    @State private var isLoadingMore = false
    @State private var hasMore = true
    
    @State private var postId: Int? = nil
    @State private var editPostCommentContent: String? = nil
    @State private var showAddPostComment: Bool = false
    
    var body: some View {
        NavigationStack {
            ZStack {
                LinearGradient(
                    colors: [Color.accent.opacity(0.5), Color.pink.opacity(0.1),Color.blue.opacity(0.2)],
                    startPoint: .topLeading,
                    endPoint: .bottomTrailing
                )
                .ignoresSafeArea()
                ScrollView {
                    LazyVStack(spacing:12) {
                        ForEach(posts, id: \.self.id) { post in
                            postItemView(post: post)
                                .onAppear {
                                    if post.id == posts.last?.id {
                                        loadMoreIfNeeded()
                                    }
                                }
                        }
                        if isLoadingMore {
                            ProgressView()
                                .padding()
                        }
                    }
                    .padding(12)
                }
                .refreshable {
                    page = 1
                    posts.removeAll()
                    getPost()
                }
            }
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button {
                        if appState.deviceMac.isEmpty {
                            appState.showBindingDeviceAlert = true
                        } else {
                            self.showAddMoment = true
                        }
                    } label: {
                        Label("Add", systemImage: "plus")
                    }
                }
            }
            .navigationTitle("Moments")
            .sheet(isPresented: $showAddMoment) {
                AddMoment(showAddMoment:$showAddMoment) { post in
                    // Add a new post
                    addPost(post: post)
                }
                .interactiveDismissDisabled(true)
            }
            .alert("Add PostComment", isPresented: $showAddPostComment) {
                TextField("Enter your comment", text: Binding(
                    get: { editPostCommentContent ?? "" },
                    set: { editPostCommentContent = $0 }
                ))
                Button(role: .cancel) {
                    showAddPostComment = false
                } label: {
                    Text("Cancel")
                }
                if #available(iOS 26.0, *) {
                    Button(role: .confirm) {
                        showAddPostComment = false
                        addPostComment()
                    } label: {
                        Text("Confirm")
                    }
                } else {
                    Button {
                        showAddPostComment = false
                        addPostComment()
                    } label: {
                        Text("Confirm")
                    }
                }
            } message: {
                Text("Please enter your comment below.")
            }
            .onAppear {
                page = 1
                posts.removeAll()
                getPost()
            }
        }
    }
    
    private func postItemView(post: Post) -> some View {
        return VStack(alignment: .leading,spacing: 12) {
            HStack {
                Image("logo_icon")
                    .resizable()
                    .frame(width: 25, height: 25)
                    .clipShape(Circle())
                Text(post.name ?? "StackChanUser")
                    .font(.system(size: 25))
                Spacer()
                if post.mac == appState.deviceMac {
                    Button(role: .destructive) {
                        deletePost(post)
                    } label: {
                        Image(systemName: "trash")
                    }
                    .glassButtonStyle()
                }
            }
            Text(post.contentText ?? "")
                .font(.body)
                .foregroundStyle(.primary)
            if let imageUrl = post.contentImage, imageUrl != "" {
                HStack {
                    AsyncImage(url: URL(string: imageUrl)) { phase in
                        switch phase {
                        case .empty:
                            ProgressView()
                                .frame(maxWidth: .infinity, maxHeight: 300)
                        case .success(let image):
                            image
                                .resizable()
                                .scaledToFit()
                                .frame(maxHeight: 300)
                        case .failure:
                            Image(systemName: "photo")
                                .resizable()
                                .scaledToFit()
                                .frame(maxHeight: 300)
                        @unknown default:
                            EmptyView()
                        }
                    }
                    Spacer()
                }
            }
            HStack(spacing: 25) {
                Text(post.createdAt ?? "")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                
                Spacer()
                
                Button(action: {
                    // Comment
                    postId = post.id
                    editPostCommentContent = ""
                    showAddPostComment = true
                }) {
                    Label("99", systemImage: "text.bubble")
                }
                
                Button(action: {
                    // Like
                }) {
                    Label("99", systemImage: "hand.thumbsup")
                }
                
                Button(action: {
                    // Share
                }) {
                    Image(systemName: "square.and.arrow.up")
                }
            }
            .padding(12)
            .font(.caption)
            .foregroundStyle(.secondary)
            
            if let comments = post.postCommentList, !comments.isEmpty {
                ForEach(comments, id: \.id) { comment in
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text((comment.name ?? "User") + ": ")
                                .font(.caption)
                                .fontWeight(.semibold)
                                .foregroundColor(.accentColor)
                            Text(comment.content ?? "")
                                .font(.caption)
                            Spacer()
                        }
                    }
                }
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity)
        .glassEffectRegular(cornerRadius: 25)
    }
    
    private func addPostComment() {
        if let content = editPostCommentContent,let postId = postId, !appState.deviceMac.isEmpty {
            let map: [String: Any] = [
                ValueConstant.mac : appState.deviceMac,
                ValueConstant.postId : postId,
                ValueConstant.content : content
            ]
            Networking.shared.post(pathUrl: Urls.postCommentCreate, parameters: map) { result in
                switch result {
                case .success(let success):
                    do {
                        let response = try Response<[String: Int]>.decode(from: success)
                        if response.isSuccess {
                            getPostComment(postId: postId)
                        }
                    } catch {
                        // Failed to parse data
                    }
                case .failure(let failure):
                    // Request failed:
                    print("Request failed:", failure)
                }
            }
        }
    }
    
    private func getPostComment(postId: Int) {
        if !appState.deviceMac.isEmpty {
            let map: [String: Any] = [
                ValueConstant.postId: postId,
                ValueConstant.mac: appState.deviceMac,
                ValueConstant.page: 1,
                ValueConstant.pageSize: 30,
            ]
            
            Networking.shared.get(pathUrl: Urls.postCommentGet,parameters: map) { result in
                switch result {
                case .success(let success):
                    do {
                        let response = try Response<GetPostComment>.decode(from: success)
                        if response.isSuccess,let list = response.data?.list {
                            for index in posts.indices {
                                if posts[index].id == postId {
                                    posts[index].postCommentList = list
                                    break
                                }
                            }
                        }
                    } catch {
                        // Failed to parse data
                    }
                case .failure(let failure):
                    // Request failed:
                    print("Request failed:", failure)
                }
            }
            
        }
        
       
    }
    
    private func addPost(post: Post) {
        let map: [String:Any] = [
            ValueConstant.mac: appState.deviceMac,
            ValueConstant.content_text: post.contentText ?? "",
            ValueConstant.content_image: post.contentImage ?? "",
        ]
        Networking.shared.post(pathUrl: Urls.postAdd, parameters: map) { result in
            switch result {
            case .success(let success):
                do {
                    let response = try Response<[String:Int]>.decode(from: success)
                    if response.isSuccess {
                        // Refresh posts
                        page = 1
                        posts.removeAll()
                        getPost()
                    }
                } catch {
                    // Failed to parse data
                }
            case .failure(let failure):
                // Request failed:
                print("Request failed:", failure)
            }
        }
    }
    
    /// Delete a post
    private func deletePost(_ post: Post) {
        let map: [String: Any] = [
            ValueConstant.id: post.id
        ]
        Networking.shared.delete(pathUrl: Urls.postDelete, parameters: map) { result in
            switch result {
            case .success(let success):
                do {
                    let response = try Response<String>.decode(from: success)
                    if response.isSuccess {
                        // Remove post locally
                        withAnimation {
                            posts.removeAll { $0.id == post.id }
                        }
                    }
                } catch {
                    // Failed to parse data
                }
            case .failure(let failure):
                // Delete failed:
                print("Delete failed:", failure)
            }
        }
    }
    
    /// Fetch post list
    private func getPost() {
        isLoadingMore = true
        let map:[String:Any] = [
            ValueConstant.page: page,
            ValueConstant.pageSize: pageSize
        ]
        Networking.shared.get(pathUrl: Urls.postGet,parameters: map) { result in
            isLoadingMore = false
            switch result {
            case .success(let success):
                do {
                    let response = try Response<[Post]>.decode(from: success)
                    if response.isSuccess,let list = response.data {
                        withAnimation {
                            if list.count < pageSize {
                                hasMore = false
                            }
                            posts.append(contentsOf: list)
                        }
                    }
                } catch {
                    
                    // Failed to parse data
                }
            case .failure(let failure):
                // Request failed:
                print("Request failed:", failure)
            }
        }
    }
    
    private func loadMoreIfNeeded() {
        guard !isLoadingMore, hasMore else { return }
        page += 1
        getPost()
    }
    
}



struct AddMoment : View {
    
    @Binding var showAddMoment: Bool
    
    var callBack: ((Post) -> Void)?
    
    @State private var post: Post = Post(id: 0)
    @State private var photoItem: PhotosPickerItem?
    @State private var isUploading: Bool = false
    
    @EnvironmentObject var appState: AppState
    
    var body: some View {
        NavigationStack {
            List {
                Section("Text") {
                    TextField("Please enter the post content", text: Binding(
                        get: {
                            post.contentText ?? ""
                        },
                        set: {
                            post.contentText = $0
                        }
                    ), axis: .vertical)
                    .textFieldStyle(.plain)
                }
                Section("image") {
                    PhotosPicker(selection: $photoItem, matching: .images) {
                        if isUploading {
                            ProgressView("Uploading...")
                        } else {
                            HStack {
                                Spacer()
                                if let urlString = post.contentImage,
                                   let url = URL(string: urlString) {
                                    AsyncImage(url: url) { phase in
                                        switch phase {
                                        case .empty:
                                            ProgressView()
                                                .frame(width: 200, height: 200)
                                        case .success(let image):
                                            image
                                                .resizable()
                                                .scaledToFit()
                                                .frame(maxHeight: 300)
                                        case .failure:
                                            Image(systemName: "photo")
                                                .frame(width: 200, height: 200)
                                        @unknown default:
                                            EmptyView()
                                        }
                                    }
                                } else {
                                    Label("Select Image", systemImage: "plus.circle")
                                }
                                Spacer()
                            }
                        }
                    }
                    .onChange(of: photoItem) { _ in
                        updateImage()
                    }
                }
            }
            .navigationTitle("Add Post")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button {
                        self.showAddMoment = false
                    } label: {
                        Label("Cancel", systemImage: "xmark")
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button {
                        callBack?(post)
                        self.showAddMoment = false
                    } label: {
                        Label("Confirm", systemImage: "checkmark")
                    }
                }
            }
        }
    }
    
    /// Upload file
    private func updateImage() {
        guard let photoItem else { return }
        isUploading = true
        Task {
            do {
                let data = try await photoItem.loadTransferable(type: Data.self)
                guard var imageData = data else {
                    // Failed to get image data
                    print("Failed to get image data")
                    isUploading = false
                    return
                }
                
                // Compress the image to no more than 2MB
                if let uiImage = UIImage(data: imageData),
                   let compressedData = uiImage.compress(toMemorySize: 2.0) {
                    imageData = compressedData
                }
                
                let map: [String:Any] = [
                    ValueConstant.file: imageData,
                    ValueConstant.directory: ValueConstant.moments,
                    ValueConstant.name: UUID().uuidString + ".jpg",
                ]
                Networking.shared.postFromData(pathUrl: Urls.uploadFile,parameters: map) { result in
                    isUploading = false
                    switch result {
                    case .success(let success):
                        do {
                            let response = try Response<UploadFile>.decode(from: success)
                            if response.isSuccess, let url = response.data?.path {
                                let fileUrl = Urls.getFileUrl() + url
                                DispatchQueue.main.async {
                                    post.contentImage = fileUrl
                                }
                            }
                        } catch {
                            // Failed to parse data
                        }
                    case .failure(let failure):
                        // Request failed:
                        print("Request failed:", failure)
                    }
                }
            } catch {
                isUploading = false
                // Failed to load image data:
                print("Failed to load image data:", error)
            }
        }
    }
}
