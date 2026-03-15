#include "nfd.h"
#include <test_viewport3d.hpp>

void Viewport3D::DrawKtxTexture(){
    static VkDescriptorSet ktxTexture = VK_NULL_HANDLE;
    static float rotation[3] = { 0.0f, 0.0f, 0.0f };
    static float position[3] = { 0.0f, 0.0f, -2.5f };
    static float zoom = 1.0f;
    static float lodBias = 0.0f;
    static bool isDragging = false;
    static ImVec2 lastMousePos;
    static bool render3D = true; // Toggle between 3D pipeline render and simple 2D
    
    // Initialize KTX texture descriptor set once when texture is loaded
    if (ktxTexture == VK_NULL_HANDLE && textureHandler.isTextureLoaded()) {
        ktxTexture = ImGui_ImplVulkan_AddTexture(
            textureHandler.getTextureSampler(),
            textureHandler.getTextureView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        DEBUG_LOG("KTX Texture registered with ImGui: %p", ktxTexture);
    }

    Begin("KTX Texture Viewer (3D)");
    Text("Image View: %p", textureHandler.getTextureView());
    
    if (ktxTexture != VK_NULL_HANDLE) {
        // Get texture dimensions
        uint32_t texWidth = textureHandler.getTextureWidth();
        uint32_t texHeight = textureHandler.getTextureHeight();
        
        // Controls section
        Text("Texture Size: %u x %u", texWidth, texHeight);
        // Checkbox("Enable 3D Pipeline Rendering", &render3D);
        Separator();
        
        // Camera/View Controls
        // Text("View Controls:");
        // DragFloat3("Rotation", rotation, 1.0f, -180.0f, 180.0f);
        // DragFloat3("Position", position, 0.1f, -10.0f, 10.0f);
        // SliderFloat("Zoom", &zoom, 0.1f, 5.0f);
        // SliderFloat("LOD Bias", &lodBias, 0.0f, 10.0f);
        
        // if (Button("Reset View")) {
        //     rotation[0] = rotation[1] = rotation[2] = 0.0f;
        //     position[0] = position[1] = 0.0f;
        //     position[2] = -2.5f;
        //     zoom = 1.0f;
        //     lodBias = 0.0f;
        // }
        
        Separator();
        
        // Update camera based on controls
        textureHandler.camera.setRotation(glm::vec3(rotation[0], rotation[1], rotation[2]));
        textureHandler.camera.setPosition(glm::vec3(position[0], position[1], position[2]));
        textureHandler.uniformData.lodBias = lodBias;
        
        // If 3D rendering is enabled, call the pipeline render
        if (render3D) {
            HandleRenderTexture();
            Text("Rendering with 3D pipeline (shaders, vertex buffers)");
        }
        
        // Calculate aspect ratio
        float aspectRatio = static_cast<float>(texWidth) / static_cast<float>(texHeight);
        
        // Get available content region
        ImVec2 contentSize = GetContentRegionAvail();
        float displayWidth = contentSize.x * zoom;
        float displayHeight = displayWidth / aspectRatio;

        // Ensure it fits within available height
        if (displayHeight > contentSize.y) {
            displayHeight = contentSize.y;
            displayWidth = displayHeight * aspectRatio;
        }

        // Limit zoom display  
        displayWidth = std::min(displayWidth, contentSize.x);
        displayHeight = std::min(displayHeight, contentSize.y);

        // Center the image
        float posX = (contentSize.x - displayWidth) * 0.5f;
        if (posX > 0) {
            SetCursorPosX(GetCursorPosX() + posX);
        }
        
        // Get image position for drag detection
        ImVec2 imagePos = GetCursorScreenPos();
        
        // Render the texture (now showing result of 3D pipeline)
        Image((ImTextureID)ktxTexture, ImVec2(displayWidth, displayHeight));
        
        // Check if mouse is over the image for drag interaction
        ImVec2 mousePos = GetMousePos();
        bool isHovering = IsItemHovered();
        
        // Handle drag rotation
        if (isHovering && IsMouseClicked(ImGuiMouseButton_Left)) {
            isDragging = true;
            lastMousePos = mousePos;
        }
        
        if (isDragging) {
            if (IsMouseDown(ImGuiMouseButton_Left)) {
                ImVec2 delta;
                delta.x = mousePos.x - lastMousePos.x;
                delta.y = mousePos.y - lastMousePos.y;
                
                rotation[1] += delta.x * 0.5f; // Horizontal drag = Y rotation
                rotation[0] += delta.y * 0.5f; // Vertical drag = X rotation
                
                // Clamp rotation
                rotation[0] = std::clamp(rotation[0], -90.0f, 90.0f);
                
                lastMousePos = mousePos;
            } else {
                isDragging = false;
            }
        }
        
        // Mouse wheel zoom
        if (isHovering) {
            float wheel = GetIO().MouseWheel;
            if (wheel != 0.0f) {
                zoom += wheel * 0.1f;
                zoom = std::clamp(zoom, 0.1f, 5.0f);
            }
        }
        
        // Show interaction hints
        Separator();
        TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drag: Rotate | Scroll: Zoom");
        Text("Current Frame: %u", currentFrame);
    } else {
        Text("Loading KTX texture...");
        if (!textureHandler.isTextureLoaded()) {
            Text("Texture not loaded yet.");
        }
    }

    End();
}

void Viewport3D::DrawViewport3D() {
    static bool isShowViewport = true;
    static float currentPosition[3] = { 0.0f, 0.0f, -2.5f }, currentRotation[3] = { 0.0f, 0.0f, -180.0f };
    static VkDescriptorSet viewportTexture = VK_NULL_HANDLE;

    // Initialize viewport texture descriptor set
    if (viewportTexture == VK_NULL_HANDLE) {
        viewportTexture = ImGui_ImplVulkan_AddTexture(
            offscreenSampler,
            imageViews[0],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }

    Begin("3D Viewport");
    Text("This is a 3D viewport using Vulkan and ImGui.");
    Text("Image View: %p", imageViews[0]);
    Text("Camera Position: ");
    SameLine();
    DragFloat3("##Camera Position", currentPosition);
    Text("Camera Rotation: ");
    SameLine();
    DragFloat3("##Camera Rotation", currentRotation);
    position = glm::vec4(currentPosition[0], currentPosition[1], currentPosition[2], 0.0f);
    rotation = glm::vec3(currentRotation[0], currentRotation[1], currentRotation[2]);
    camera.setPosition(position);
    camera.setRotation(rotation);
    // showCurrentCameraPosition();
    Text("Camera Rotation: (%.2f, %.2f, %.2f)", camera.rotation.x, camera.rotation.y, camera.rotation.z);
   
    Checkbox("Show Viewport", &isShowViewport);
    if (isShowViewport) {
        HandleRenderViewport(viewportWidth, viewportHeight);
    }

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    // Update viewport dimensions if window resized
    if (viewportWidth != static_cast<int>(viewportSize.x) || viewportHeight != static_cast<int>(viewportSize.y)) {
        viewportWidth = static_cast<int>(viewportSize.x);
        viewportHeight = static_cast<int>(viewportSize.y);
    }

    Text("Viewport Size: %d x %d", viewportWidth, viewportHeight);

    // Get cursor position and viewport position
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImVec2 mousePos = ImGui::GetMousePos();

    Text("Mouse Position: (%.2f, %.2f)", mousePos.x, mousePos.y);
    Text("Viewport Position: (%.2f, %.2f)", cursorPos.x, cursorPos.y);
    // Check if mouse is inside the viewport
    bool isMouseInsideViewport = mousePos.x >= cursorPos.x &&
                                 mousePos.y >= cursorPos.y &&
                                 mousePos.x <= cursorPos.x + viewportSize.x &&
                                 mousePos.y <= cursorPos.y + viewportSize.y;

    // Render the viewport image
    Image((ImTextureID)viewportTexture, viewportSize);

    // Handle WASD input if mouse is inside the viewport
    if (isMouseInsideViewport) {
        HandleCameraMovement();
    }

    End();
}

void Viewport3D::HandleCameraMovement() {
    const float deltaTime = 0.016f; // Simulasi waktu frame (60 FPS)
    const float moveSpeed = camera.movementSpeed * deltaTime;

    // Check ImGui IO for key presses
    ImGuiIO& io = ImGui::GetIO();

    const Uint8* state = reinterpret_cast<const Uint8*>(SDL_GetKeyboardState(NULL));
    if (state[SDL_SCANCODE_W]) {
        Log("W pressed", LogLevel::INFO);
        camera.keys.up;
    }
    if (state[SDL_SCANCODE_S]) {
        Log("S pressed", LogLevel::INFO);
        camera.keys.down;
    }
    if (state[SDL_SCANCODE_A]) {
        Log("A pressed", LogLevel::INFO);
        camera.keys.left;
    }
    if (state[SDL_SCANCODE_D]) {
        Log("D pressed", LogLevel::INFO);
        camera.keys.right;
    }

    // Update camera based on movement keys
    camera.update(deltaTime);
}


void Viewport3D::videoPlayerUI(){
    static char videoPath[5000] = "/home/hylmi/GameEngineFolder/My First Project/assets/videos/Usagi Flap.mp4";
    static bool loopVideo = true;
    static bool paused = false;
    // enum RenderMode currentMode;
    static bool isOnlyRender = imageHandler.isOnlyRenderImage;
    static bool isOnlyAd = imageHandler.isOnlyAudio;

    Begin("Video Player");
    InputText("Video File", videoPath, IM_ARRAYSIZE(videoPath));
    SameLine();
    if (Button("Open")) {
        if (strlen(videoPath) > 0) {
            imageHandler.OpenFileVideo(videoPath);
            paused = false;
        }
    }

    Checkbox("Loop Video", &loopVideo);
    SameLine();
    Checkbox("Only Render Image", &isOnlyRender);
    SameLine();
    Checkbox("Only Audio", &isOnlyAd);

    if (imageHandler.isPlaying) {  
        if (!paused){
            if (isOnlyRender == true && isOnlyAd == false) {
                // updateVideoFrame();
                Log("Only Render Image", LogLevel::WARNING);
                imageHandler.updateVideoFrame();
            }
            else if (isOnlyRender == false && isOnlyAd == true) {
                // Log("Only Render audio", LogLevel::WARNING);
                imageHandler.updateAudio();
            }
            else {
                // if (imageHandler.fps <= 24) {
                    // imageHandler.updateBothVideoAndAudio24fps();
                // } else {
                    imageHandler.updateBothVideoAndAudio();
                // }
            }
        }

        renderVideoFrame();

        Separator();
        if (Button(paused ? "Play" : "Pause")) {
                paused = !paused;
            }
            SameLine();
            if (Button("Stop")) {
                av_seek_frame(imageHandler.formatContext, imageHandler.videoStream, 0, AVSEEK_FLAG_BACKWARD);
                imageHandler.currentTime = 0;
                paused = true;
            }

            Text("Time: %.2f / %.2f", imageHandler.currentTime, imageHandler.duration);

            // Informasi video
            Text("Resolution: %dx%d | FPS: %.2f", 
                        imageHandler.width, imageHandler.height, imageHandler.fps);
            if (!isOnlyRender | isOnlyAd) {
                Separator();
                Text("Frequency: %d | Channels: %d", 
                        imageHandler.audioCodecContext->sample_rate, imageHandler.audioCodecContext->ch_layout.nb_channels);
            }
    } else {
        Text("No video loaded or playing.");
    }

    End();
}

// Draw Image
void Viewport3D::DrawImage()
{
    static VkDescriptorSet imguiTexture = VK_NULL_HANDLE;
    if (imguiTexture == VK_NULL_HANDLE){
        imguiTexture = imageHandler.LoadImage("assets/images/backgrounds/Hanako_Swimsuit.png");
    }
    Begin("Image Test");

    Text("Current File: %s", imageHandler.currentFile.c_str());
    if (Button("Load Image")) {
        nfdfilteritem_t listItem[1] = {
            {"Image", "png,jpg,jpeg,bmp,tga,tiff,webp,gif,svg"}
        };
        string newImage = utilities.OpenFile(listItem, 1);
        if (newImage != "") {
            imguiTexture = imageHandler.LoadImage(newImage.c_str());
        }
    }
    float aspectRatio = static_cast<float>(1280) / static_cast<float>(720);
        
    // Hitung dimensi tampilan
    ImVec2 contentSize = GetContentRegionAvail();
    float displayWidth = contentSize.x;
    float displayHeight = displayWidth / aspectRatio;

    if (displayHeight > contentSize.y) {
        displayHeight = contentSize.y;
        displayWidth = displayHeight * aspectRatio;
    }

    // Pusatkan video di ruang yang tersedia  
    float posX = (contentSize.x - displayWidth) * 0.5f;
    float posY = (contentSize.y - displayHeight) * 0.5f;
    
    SetCursorPos(ImVec2(posX, posY));
    
    float w = displayWidth, h = displayHeight;

    Image((ImTextureID)imguiTexture, ImVec2(w, h));
    End();
}
