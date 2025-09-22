#include <vulkanhandler.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stdexcept>

VkDescriptorSet VulkanHandler::LoadImage(const char* filename){
     // Load PNG pakai stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels)
        throw runtime_error("Failed to load texture image!");

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    
    Debug::Logger::Log("Texture size: "+to_string(imageSize));
    // Buat image yang langsung bisa diakses CPU (HOST_VISIBLE)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_LINEAR; // langsung bisa diakses CPU
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage textureImage;
    if (vkCreateImage(currentDevice, &imageInfo, nullptr, &textureImage) != VK_SUCCESS)
        throw runtime_error("Failed to create image!");
        
    Debug::Logger::Log("Texture image: "+to_string(reinterpret_cast<uintptr_t>(textureImage)));

    // Alokasi memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(currentDevice, textureImage, &memRequirements);
    cout << "Mem requirements size: " << memRequirements.size << endl;
    VkMemoryAllocateInfo allocInfo = createMemoryAllocateInfo(memRequirements, currentPhysicalDevice);

    VkDeviceMemory textureMemory;
    if (vkAllocateMemory(currentDevice, &allocInfo, nullptr, &textureMemory) != VK_SUCCESS)
        throw runtime_error("Failed to allocate image memory!");
    cout << "Allocinfo: " << allocInfo.allocationSize << endl;

    vkBindImageMemory(currentDevice, textureImage, textureMemory, 0);

    // Copy pixel data langsung ke image
    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(currentDevice, textureImage, &subresource, &layout);

    void* data;
    vkMapMemory(currentDevice, textureMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(currentDevice, textureMemory);

    stbi_image_free(pixels);
    cout << "Texture memory: " << textureMemory << endl;
    cout << "subresource: " << subresource.aspectMask << endl;
    cout << "Sucresource layout: " << layout.offset << endl;
    // Buat ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView textureImageView;
    if (vkCreateImageView(currentDevice, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS)
        throw runtime_error("Failed to create texture image view!");

    cout << "View Info: "<< textureImageView << endl;
    // Buat Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler textureSampler;
    if (vkCreateSampler(currentDevice, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS)
        throw runtime_error("Failed to create texture sampler!");
    // cout << "TextureSampler loaded: " << textureSampler << endl;
    // cout << "TextureImageView loaded: " << textureImageView << endl;
    Debug::Logger::Log("TextureSampler loaded: " + to_string(reinterpret_cast<uintptr_t>(textureSampler)));
    Debug::Logger::Log("TextureImageView loaded: " + to_string(reinterpret_cast<uintptr_t>(textureImageView)));
    // Tambahin ke ImGui
    VkDescriptorSet imguiDescSet = ImGui_ImplVulkan_AddTexture(
        textureSampler,
        textureImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    return imguiDescSet;
}

void VulkanHandler::setCurrentDeviceAndPhysic(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, uint32_t currentGraphicQueue, VkCommandPool commandPool) {
    Logger::Log("Setting current device and physical device", LogLevel::SUCCESS);
    currentDevice = device;
    currentPhysicalDevice = physicalDevice;
    currentOffscreenCommandPool = commandPool;
    currentGraphicsQueue = graphicsQueue;

    // VkCommandPoolCreateInfo poolInfo{};
    // poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // poolInfo.queueFamilyIndex = &currentGraphicsQueue; // atau graphicsQueueFamily
}

void VulkanHandler::cleanUpVideoHandler(){
    if (buffer) av_free(buffer);
    if (frameRGB) av_frame_free(&frameRGB);
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
    if (swsContext) sws_freeContext(swsContext);
    // if (audioStream) {
    //     SDL_DestroyAudioStream(audioStream);
    //     audioStream = nullptr;
    // }
    
    // if (audioDeviceID) {
    //     SDL_CloseAudioDevice(audioDeviceID);
    //     audioDeviceID = 0;
    // }
    
    // if (swrContext) {
    //     swr_free(&swrContext);
    // }
    
    // if (audioCodecContext) {
    //     avcodec_free_context(&audioCodecContext);
    // }
    
    // av_channel_layout_uninit(&audioChannelLayout);
    // isPlayingAudio = false;

    formatContext = nullptr;
    codecContext = nullptr;
    swsContext = nullptr;
    frame = nullptr;
    frameRGB = nullptr;
    packet = nullptr;
    buffer = nullptr;
    videoStream = -1;
    isPlaying = false;
}

bool VulkanHandler::OpenFileVideo(const char* filePath){

    if (isPlaying) {
        cleanUpVideoHandler();
    }

    if (avformat_open_input(&formatContext, filePath, NULL, NULL) != 0) {
        cerr << "[MainWindow] Could not open video file: " << filePath << endl;
        return false;
    }
    else {
        cout << "[MainWindow] Video file opened: " << filePath << endl;
    }

    if (avformat_find_stream_info(formatContext, NULL) < 0) {
        cerr << "Could not find stream information" << endl;
        cleanUpVideoHandler();
        return false;
    }
    else {
        cout << "[MainWindow] Video stream information found " << formatContext << endl;
    }

    cout << "[MainWindow] Searching for video stream" << endl;
    videoStream = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    
    cout << "[MainWindow] Video stream found: " << videoStream << endl;
    if (videoStream == -1) {
        cerr << "Could not find video stream" << endl;
        cleanUpVideoHandler();
        return false;
    }

    // Mendapatkan codec
    cout << "[MainWindow] Getting codec" << endl;
    const AVCodec *codec = avcodec_find_decoder(
        formatContext->streams[videoStream]->codecpar->codec_id);
    if (!codec) {
        cerr << "Unsupported codec" << endl;
        cleanUpVideoHandler();
        return false;
    }

    // Alokasi context codec
    cout << "[MainWindow] Allocating codec context" << endl;
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        cerr << "Could not allocate codec context" << endl;
        cleanUpVideoHandler();
        return false;
    }
    
    // Copy parameters dari stream ke codec context
    cout << "[MainWindow] Copying codec parameters" << endl;
    if (avcodec_parameters_to_context(codecContext, 
        formatContext->streams[videoStream]->codecpar) < 0) {
        cerr << "Could not copy codec parameters" << endl;
        cleanUpVideoHandler();
        return false;
    }
    
    // Buka codec
    cout << "[MainWindow] Opening codec" << endl;
    if (avcodec_open2(codecContext, codec, NULL) < 0) {
        cerr << "Could not open codec" << endl;
        cleanUpVideoHandler();
        return false;
    }

    // Alokasi frame
    cout << "[MainWindow] Allocating frames" << endl;
    frame = av_frame_alloc();
    frameRGB = av_frame_alloc();
    // hw_frame = av_frame_alloc();

    if (!frame || !frameRGB /*|| !hw_frame*/) {
        cerr << "Could not allocate frames" << endl;
        cleanUpVideoHandler();
        return false;
    }
    
    // Determine required buffer size and allocate buffer
    cout << "[MainWindow] Determining buffer size and allocating buffer" << endl;
    width = codecContext->width;
    height = codecContext->height;
    
    cout << "[MainWindow] Video size: " << width << "x" << height << endl;
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, 
                                          height, 1);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    
    av_image_fill_arrays(frameRGB->data, frameRGB->linesize, 
                        buffer, AV_PIX_FMT_RGB24,
                        width, height, 1);
    
    lastGoodFrameRGB = av_frame_alloc();
    av_image_alloc(lastGoodFrameRGB->data, lastGoodFrameRGB->linesize,
                  width, height, AV_PIX_FMT_RGB24, 1);
                  hasValidFrame = false;
    
    // Initialize SWS context for software scaling
    cout << "[MainWindow] Initializing SWS context" << endl;
    swsContext = sws_getContext(
        width, height, codecContext->pix_fmt,
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
    
    if (!swsContext) {
        cerr << "Could not initialize SWS context" << endl;
        cleanUpVideoHandler();
        return false;
    }

    // Baca frame rate
    AVRational frameRate = formatContext->streams[videoStream]->avg_frame_rate;
    fps = (double)frameRate.num / (double)frameRate.den;

    currentTime = 0;
    
    // Baca durasi video
    duration = formatContext->duration / 1000000.0;
    
    
    // Alokasi packet
    cout << "[MainWindow] Allocating packet" << endl;
    packet = av_packet_alloc();
    if (!packet) {
        cerr << "Could not allocate packet" << endl;
        cleanUpVideoHandler();
        return false;
    }

    cout << "[MainWindow] Creating Vulkan texture" << endl;
    if (!createVideoTexture()) {
        cerr << "Could not create Vulkan texture" << endl;
        cleanUpVideoHandler();
        return false;
    }

    // Note Kalau Asal Ubah bisa error tanpa Log
    cout << "[MainWindow] Opening audio" << endl;
    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    cout << "[MainWindow] Audio stream: " << audioStream << endl;
    if (audioStreamIndex){
        openAudio();
    } else {
        Logger::Log("Audio Stream Not found", LogLevel::CRASH);
    }

    isPlaying = true;
    cout << "[MainWindow] Video handler initialized" << endl;
    return isPlaying;
}

bool VulkanHandler::createVideoTexture() {
    VkDeviceSize imageSize = width * height * 3; // RGB24 format

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(currentDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        cerr << "Failed to create staging buffer!" << endl;
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(currentDevice, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(currentDevice, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        cerr << "Failed to allocate staging buffer memory!" << endl;
        return false;
    }

    vkBindBufferMemory(currentDevice, stagingBuffer, stagingBufferMemory, 0);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8_UNORM; // RGB format
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(currentDevice, &imageInfo, nullptr, &videoImage) != VK_SUCCESS) {
        cerr << "Failed to create image!" << endl;
        return false;
    }

    vkGetImageMemoryRequirements(currentDevice, videoImage, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(currentDevice, &allocInfo, nullptr, &videoImageMemory) != VK_SUCCESS) {
        cerr << "Failed to allocate image memory!" << endl;
        return false;
    }

    vkBindImageMemory(currentDevice, videoImage, videoImageMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = videoImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(currentDevice, &viewInfo, nullptr, &videoImageView) != VK_SUCCESS) {
        cerr << "Failed to create texture image view!" << endl;
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(currentDevice, &samplerInfo, nullptr, &videoSampler) != VK_SUCCESS) {
        cerr << "Failed to create texture sampler!" << endl;
        return false;
    }

    // Create descriptor set for ImGui
    videoDescriptorSet = ImGui_ImplVulkan_AddTexture(videoSampler, videoImageView, 
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    Logger::Log("Video texture created", LogLevel::SUCCESS);
    return true;
}

void VulkanHandler::updateVideoTexture() {
    VkDeviceSize imageSize = width * height * 3; // RGB24 format

    // Copy frame data to staging buffer
    void* data;
    vkMapMemory(currentDevice, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, frameRGB->data[0], static_cast<size_t>(imageSize));
    vkUnmapMemory(currentDevice, stagingBufferMemory);

    // Transition image layout and copy buffer to image
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = videoImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, videoImage, 
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

bool VulkanHandler::updateVideoFrame() {
    if (!isPlaying) return false;

    int frameFinished = 0;
    int readAttempts = 0;
    const float MAX_READ_ATTEMPTS = 1.0f;

    while (!frameFinished && readAttempts < MAX_READ_ATTEMPTS) {
        readAttempts++;
        int readResult = av_read_frame(formatContext, packet);
        if (readResult < 0) {
            if (readResult == AVERROR_EOF) {
                av_seek_frame(formatContext, videoStream, 0, AVSEEK_FLAG_BACKWARD);
                continue;
            } else {
                SDL_Delay(5);
                continue;
            }
        }

        if (packet->stream_index == videoStream) {
            int sendResult = avcodec_send_packet(codecContext, packet);
            av_packet_unref(packet);

            if (sendResult < 0) continue;

            int receiveResult = avcodec_receive_frame(codecContext, frame);
            if (receiveResult < 0) {
                if (receiveResult == AVERROR(EAGAIN)) continue;
                else if (receiveResult == AVERROR_EOF) break;
                else continue;
            } else {
                frameFinished = 1;
                break;
            }
        } else {
            av_packet_unref(packet);
        }
    }

    if (frameFinished) {
        sws_scale(swsContext, (uint8_t const* const*)frame->data,
                  frame->linesize, 0, height,
                  frameRGB->data, frameRGB->linesize);

        // SDL_UpdateTexture(texture, NULL, frameRGB->data[0],
        //                   frameRGB->linesize[0]);

        // glBindTexture(GL_TEXTURE_2D, glTextureID);
        // glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
        //         GL_RGB, GL_UNSIGNED_BYTE, frameRGB->data[0]);
        updateVideoTexture();

        // Update current time using frame PTS
        if (frame->pts != AV_NOPTS_VALUE) {
            AVRational timeBase = formatContext->streams[videoStream]->time_base;
            currentTime = frame->pts * av_q2d(timeBase);
        }

        return true;
    }

    return false;
}

bool VulkanHandler::openAudio() {
    // Find the best audio stream
    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO,
                                      -1, -1, nullptr, 0);
    if (audioStreamIndex < 0) {
        cerr << "Could not find audio stream" << endl;
        return false;
    }

    // Get the stream and find decoder
    AVStream *st = formatContext->streams[audioStreamIndex];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        cerr << "Could not find audio decoder" << endl;
        return false;
    }

    // Allocate codec context
    audioCodecContext = avcodec_alloc_context3(dec);
    if (!audioCodecContext) {
        cerr << "Could not allocate audio codec context" << endl;
        return false;
    }
    
    // Copy parameters to context
    if (avcodec_parameters_to_context(audioCodecContext, st->codecpar) < 0) {
        cerr << "Could not copy audio codec parameters to context" << endl;
        avcodec_free_context(&audioCodecContext);
        return false;
    }
    
    // Open the codec
    if (avcodec_open2(audioCodecContext, dec, nullptr) < 0) {
        cerr << "Could not open audio codec" << endl;
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    // Initialize audio resampler
    swrContext = swr_alloc();
    if (!swrContext) {
        cerr << "Could not allocate resampler context" << endl;
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    // Set up input channel layout (using the codec context)
    AVChannelLayout in_layout = audioCodecContext->ch_layout;
    
    // Set up output channel layout (stereo)
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 2); // stereo
    
    // Store the output layout for later use
    av_channel_layout_copy(&audioChannelLayout, &out_layout);

    // Configure the resampler
    av_opt_set_chlayout(swrContext, "in_chlayout", &in_layout, 0);
    av_opt_set_chlayout(swrContext, "out_chlayout", &out_layout, 0);
    av_opt_set_int(swrContext, "in_sample_rate", audioCodecContext->sample_rate, 0);
    av_opt_set_int(swrContext, "out_sample_rate", 44100, 0); // Fixed output rate for SDL3
    av_opt_set_sample_fmt(swrContext, "in_sample_fmt", audioCodecContext->sample_fmt, 0);
    av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    // Initialize the resampler
    int ret = swr_init(swrContext);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        cerr << "Could not initialize the resampler: " << errbuf << endl;
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }
    cout << "SWR Context: " << swrContext << endl;

    // SDL3: Define audio specifications
    SDL_AudioSpec srcSpec, dstSpec;
    
    // Source specification (from FFmpeg)
    SDL_zero(srcSpec);
    srcSpec.freq = audioCodecContext->sample_rate;
    srcSpec.format = SDL_AUDIO_S16;
    srcSpec.channels = audioCodecContext->ch_layout.nb_channels;
    
    // Destination specification (what we want for playback)
    SDL_zero(dstSpec);
    dstSpec.freq = 44100; // Standard output rate
    dstSpec.format = SDL_AUDIO_S16;
    dstSpec.channels = 2; // Stereo output

    // SDL3: Open audio device first
    audioDeviceID = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!audioDeviceID) {
        cerr << "Failed to open audio device: " << SDL_GetError() << endl;
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    // SDL3: Create audio stream for format conversion and buffering
    audioStream = SDL_CreateAudioStream(&srcSpec, &dstSpec);
    if (!audioStream) {
        cerr << "Failed to create audio stream: " << SDL_GetError() << endl;
        SDL_CloseAudioDevice(audioDeviceID);
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    // SDL3: Bind the stream to the device
    if (!SDL_BindAudioStream(audioDeviceID, audioStream)) {
        cerr << "Failed to bind audio stream: " << SDL_GetError() << endl;
        SDL_DestroyAudioStream(audioStream);
        SDL_CloseAudioDevice(audioDeviceID);
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    // SDL3: Resume audio device (replaces SDL_PauseAudioDevice)
    if (!SDL_ResumeAudioDevice(audioDeviceID)) {
        cerr << "Failed to resume audio device: " << SDL_GetError() << endl;
        SDL_DestroyAudioStream(audioStream);
        SDL_CloseAudioDevice(audioDeviceID);
        swr_free(&swrContext);
        avcodec_free_context(&audioCodecContext);
        return false;
    }

    cout << "Audio initialized successfully" << endl;
    cout << "Input: " << srcSpec.freq << "Hz, " << (int)srcSpec.channels << " channels" << endl;
    cout << "Output: " << dstSpec.freq << "Hz, " << (int)dstSpec.channels << " channels" << endl;
    
    isPlayingAudio = true;
    
    return isPlayingAudio;
}

void VulkanHandler::updateAudio() {
    if (!audioCodecContext || !swrContext || audioStream == nullptr) {
        // Logger::Log("Audio not initialized");
        return; // Audio not initialized
    }

    if (!isPlaying) {
        Logger::Log("Video not playing");
        return; // Video not playing
    }

    // cout << "Updating audio..." << endl;

    // Check if audio queue is getting too full
    const int MAX_AUDIO_QUEUE_SIZE = 8192 * 64;    // Lebih besar
    const int IDEAL_QUEUE_SIZE = 8192 * 32;        // Target size
    
    // Check if we need more audio - SDL3 uses SDL_GetAudioStreamQueued
    Uint32 currentQueueSize = SDL_GetAudioStreamQueued(audioStream);
    if (currentQueueSize > IDEAL_QUEUE_SIZE) {
        return; // Cukup audio dalam buffer
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        cerr << "Could not allocate packet" << endl;
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        cerr << "Could not allocate frame" << endl;
        av_packet_free(&pkt);
        return;
    }

    bool packetProcessed = false;
    int readResult;

    // Keep reading frames until we process an audio packet or reach end of file
    while (!packetProcessed && (readResult = av_read_frame(formatContext, pkt)) >= 0) {
        if (pkt->stream_index == audioStreamIndex) {  // Assuming you have audioStreamIndex
            int sendResult = avcodec_send_packet(audioCodecContext, pkt);
            if (sendResult < 0) {
                char errbuf[256];
                av_strerror(sendResult, errbuf, sizeof(errbuf));
                cerr << "Error sending audio packet to decoder: " << errbuf << endl;
                av_packet_unref(pkt);
                continue;
            }

            while (true) {
                int receiveResult = avcodec_receive_frame(audioCodecContext, frame);
                if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                    // Need more packets or end of file
                    break;
                } else if (receiveResult < 0) {
                    // Error
                    char errbuf[256];
                    av_strerror(receiveResult, errbuf, sizeof(errbuf));
                    cerr << "Error receiving audio frame: " << errbuf << endl;
                    break;
                }

                // Calculate output channel count (always 2 for stereo)
                int outChannels = 2;
                
                // Calculate buffer size needed
                int outSamples = av_rescale_rnd(
                    swr_get_delay(swrContext, audioCodecContext->sample_rate) + frame->nb_samples,
                    44100, // output rate
                    audioCodecContext->sample_rate,
                    AV_ROUND_UP
                );

                // Allocate output buffer
                uint8_t* outBuf = nullptr;
                int outLinesize = 0;
                int allocResult = av_samples_alloc(
                    &outBuf, &outLinesize,
                    outChannels,
                    outSamples,
                    AV_SAMPLE_FMT_S16, 0
                );
                
                if (allocResult < 0) {
                    char errbuf[256];
                    av_strerror(allocResult, errbuf, sizeof(errbuf));
                    cerr << "Could not allocate audio output buffer: " << errbuf << endl;
                    break;
                }

                // Convert audio samples
                int convertedSamples = swr_convert(
                    swrContext,
                    &outBuf, outSamples,
                    (const uint8_t**)frame->data, frame->nb_samples
                );
                
                if (convertedSamples < 0) {
                    char errbuf[256];
                    av_strerror(convertedSamples, errbuf, sizeof(errbuf));
                    cerr << "Error converting audio: " << errbuf << endl;
                    av_freep(&outBuf);
                    break;
                }

                // Calculate size of converted data in bytes
                int outBufSize = av_samples_get_buffer_size(
                    nullptr, outChannels,
                    convertedSamples,
                    AV_SAMPLE_FMT_S16, 1
                );
                
                if (outBufSize > 0) {
                    // SDL3: Use SDL_PutAudioStreamData instead of SDL_QueueAudio
                    if (SDL_PutAudioStreamData(audioStream, outBuf, outBufSize) < 0) {
                        cerr << "Failed to put audio stream data: " << SDL_GetError() << endl;
                    } else {
                        packetProcessed = true;
                    }
                }

                // Free the output buffer
                av_freep(&outBuf);
            }
        }
        av_packet_unref(pkt);
    }

    // Handle end of file or error
    if (readResult < 0 && readResult != AVERROR_EOF) {
        char errbuf[256];
        av_strerror(readResult, errbuf, sizeof(errbuf));
        cerr << "Error reading frame: " << errbuf << endl;
    } else if (readResult == AVERROR_EOF) {
        // End of file - seek back to beginning (for looping)
        int seekResult = av_seek_frame(formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
        if (seekResult < 0) {
            char errbuf[256];
            av_strerror(seekResult, errbuf, sizeof(errbuf));
            cerr << "Error seeking to beginning: " << errbuf << endl;
        } else {
            // Flush the codec buffers
            avcodec_flush_buffers(audioCodecContext);
        }
    }

    // SDL3: Use SDL_GetAudioStreamQueued instead of SDL_GetQueuedAudioSize
    const int AUDIO_QUEUE_SIZE = SDL_GetAudioStreamQueued(audioStream);
    cout << "AUDIO_QUEUE_SIZE: " << AUDIO_QUEUE_SIZE << endl;
    // log("Audio Queue Size: "+AUDIO_QUEUE_SIZE);

    // Clean up
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void VulkanHandler::updateBothVideoAndAudio(){
    if (!isPlaying || !formatContext) {
        return; // Video not initialized
    }

    // Check if audio is available
    bool hasAudio = (audioCodecContext && swrContext && audioDeviceID != 0 && audioStream != nullptr);
    
    // Audio buffer constants for SDL3
    const int MAX_AUDIO_QUEUE_SIZE = 8192 * 64;    // Maximum buffer size
    const int IDEAL_AUDIO_QUEUE_SIZE = 8192 * 32;  // Target buffer level
    const int MIN_AUDIO_QUEUE_SIZE = 8192 * 16;    // Threshold to refill
    
    // Track if we need to prioritize audio processing
    bool needMoreAudio = false;
    if (hasAudio) {
        // SDL3: Use SDL_GetAudioStreamQueued instead of SDL_GetQueuedAudioSize
        needMoreAudio = (SDL_GetAudioStreamQueued(audioStream) < MIN_AUDIO_QUEUE_SIZE);
    }

    bool videoFrameProcessed = false;
    
    // Pre-fill audio buffer if needed
    if (hasAudio && needMoreAudio) {
        for (int i = 0; i < 5 && SDL_GetAudioStreamQueued(audioStream) < MIN_AUDIO_QUEUE_SIZE; i++) {
            updateAudio();
        }
    }

    int maxPacketsToProcess = hasAudio && needMoreAudio ? 5 : 2;
    int packetsProcessed = 0;

    while (packetsProcessed < maxPacketsToProcess || !videoFrameProcessed) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            Logger::Log("Could not allocate packet", LogLevel::CRASH);
            break;
        }

        int readResult = av_read_frame(formatContext, pkt);

        if (readResult < 0) {
            av_packet_free(&pkt);
            
            if (readResult == AVERROR_EOF) {
                // Handle end of file - seek to beginning for looping
                int seekResult = av_seek_frame(formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
                if (seekResult < 0) {
                    char errbuf[256];
                    av_strerror(seekResult, errbuf, sizeof(errbuf));
                    Logger::Log("Error seeking to beginning: " + std::string(errbuf), LogLevel::CRASH);
                } else {
                    // Flush codec buffers
                    if (codecContext) avcodec_flush_buffers(codecContext);
                    if (audioCodecContext) avcodec_flush_buffers(audioCodecContext);
                }
            } else {
                char errbuf[256];
                av_strerror(readResult, errbuf, sizeof(errbuf));
                Logger::Log("Error reading frame: " + std::string(errbuf), LogLevel::CRASH);
            }
            break;
        }

        packetsProcessed++;

        // Process video packets first to prevent glitching
        if (pkt->stream_index == videoStream && !videoFrameProcessed) {
            if (processVideoPacket(pkt)) {
                videoFrameProcessed = true;
            }
        }
        // Process audio packets
        else if (hasAudio && pkt->stream_index == audioStreamIndex) {
            processAudioPacket(pkt);
            
            // SDL3: Check audio buffer status
            if (SDL_GetAudioStreamQueued(audioStream) >= IDEAL_AUDIO_QUEUE_SIZE) {
                needMoreAudio = false;
            }
        }
        
        av_packet_unref(pkt);
        av_packet_free(&pkt);
        
        // Break if we have enough video and audio data
        if (videoFrameProcessed && (!hasAudio || !needMoreAudio || 
            SDL_GetAudioStreamQueued(audioStream) >= MIN_AUDIO_QUEUE_SIZE)) {
            break;
        }
        
        // Safety check to prevent infinite loops
        if (packetsProcessed > 20 && !videoFrameProcessed) {
            Logger::Log("Warning: Processed 20 packets without finding a video frame", LogLevel::WARNING);
            break;
        }
    }
    
    // Debug audio buffer status
    if (hasAudio) {
        const int AUDIO_QUEUE_SIZE = SDL_GetAudioStreamQueued(audioStream);
        if (AUDIO_QUEUE_SIZE < MIN_AUDIO_QUEUE_SIZE) {
            Logger::Log("Low audio buffer: " + std::to_string(AUDIO_QUEUE_SIZE) + " bytes", LogLevel::WARNING);
        }
    }
}

bool VulkanHandler::processVideoPacket(AVPacket* pkt) {
    int sendResult = avcodec_send_packet(codecContext, pkt);
    if (sendResult < 0) {
        return false;
    }
    
    int receiveResult = avcodec_receive_frame(codecContext, frame);
    if (receiveResult < 0) {
        return false;
    }
    
    // Successfully got a video frame - convert it
    sws_scale(swsContext, 
            (uint8_t const* const*)frame->data,
            frame->linesize, 0, height,
            frameRGB->data, frameRGB->linesize);

    // Update both SDL texture and OpenGL texture
    // SDL_UpdateTexture(texture, NULL, 
    //                 frameRGB->data[0],
    //                 frameRGB->linesize[0]);
                    
    updateVideoTexture();

    // Update current time using frame PTS
    if (frame->pts != AV_NOPTS_VALUE) {
        AVRational timeBase = formatContext->streams[videoStream]->time_base;
        currentTime = frame->pts * av_q2d(timeBase);
    }
    
    return true;
}

void VulkanHandler::processAudioPacket(AVPacket* pkt) {
    AVFrame* audioFrame = av_frame_alloc();
    if (!audioFrame) {
        cerr << "Could not allocate audio frame" << endl;
        return;
    }
    
    int sendResult = avcodec_send_packet(audioCodecContext, pkt);
    if (sendResult < 0) {
        av_frame_free(&audioFrame);
        return;
    }
    
    // Try to receive multiple frames from this packet if available
    while (true) {
        int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
        if (receiveResult < 0) {
            // No more frames or error
            break;
        }
        
        // Process audio frame
        int outChannels = 2; // Stereo output
        
        // Calculate buffer size needed
        int outSamples = av_rescale_rnd(
            swr_get_delay(swrContext, audioCodecContext->sample_rate) + audioFrame->nb_samples,
            44100, // output rate
            audioCodecContext->sample_rate,
            AV_ROUND_UP
        );

        uint8_t* outBuf = nullptr;
        int outLinesize = 0;
        int allocResult = av_samples_alloc(
            &outBuf, &outLinesize,
            outChannels,
            outSamples,
            AV_SAMPLE_FMT_S16, 0
        );
        
        if (allocResult >= 0) {
            int convertedSamples = swr_convert(
                swrContext,
                &outBuf, outSamples,
                (const uint8_t**)audioFrame->data, audioFrame->nb_samples
            );
            
            if (convertedSamples >= 0) {
                int outBufSize = av_samples_get_buffer_size(
                    nullptr, outChannels,
                    convertedSamples,
                    AV_SAMPLE_FMT_S16, 1
                );
                
                if (outBufSize > 0) {
                    if (SDL_PutAudioStreamData(audioStream, outBuf, outBufSize) < 0) {
                        cerr << "Failed to put audio stream data: " << SDL_GetError() << endl;
                    }
                }
            }

            av_freep(&outBuf);
        }
        
        // Reset frame for reuse
        av_frame_unref(audioFrame);
    }
    
    av_frame_free(&audioFrame);
}