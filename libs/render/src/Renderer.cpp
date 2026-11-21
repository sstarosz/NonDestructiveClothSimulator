#include "clothcad/render/Renderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <utility>  // before Vulkan-Hpp: some of its versions use std::exchange without it

// Vulkan's functions are found when the program runs (vk::raii::Context
// loads the library), so nothing is linked against Vulkan.
#include <vulkan/vulkan_raii.hpp>

namespace clothcad::render {

namespace {

// The shaders, compiled to SPIR-V when the project is built.
#include "shaders/surface.vert.h"
#include "shaders/surface.frag.h"
#include "shaders/line.frag.h"

// What the shaders are handed with every draw: 96 bytes, within the 128 every card allows.
struct Push {
    Matrix viewProjection;
    Colour colour;
    std::array<float, 4> toLight;
};

struct Vertex {
    Point position;
    Point normal;
};

constexpr vk::Format pictureFormat = vk::Format::eR8G8B8A8Unorm;

std::uint32_t memoryType(const vk::raii::PhysicalDevice& physical, std::uint32_t allowed, vk::MemoryPropertyFlags wanted) {
    const vk::PhysicalDeviceMemoryProperties memory = physical.getMemoryProperties();
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((allowed & (1u << i)) && (memory.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    throw std::runtime_error("The graphics card has no memory of the kind needed.");
}

// A buffer the processor writes and the card reads; it grows as needed.
struct Buffer {
    vk::raii::Buffer buffer{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    vk::DeviceSize size = 0;
    void* mapped = nullptr;

    void fit(const vk::raii::Device& device, const vk::raii::PhysicalDevice& physical, vk::DeviceSize needed, vk::BufferUsageFlags usage) {
        if (needed <= size) return;
        mapped = nullptr;
        buffer = nullptr;
        memory = nullptr;
        size = std::max<vk::DeviceSize>(needed + needed / 2, 64 * 1024);
        buffer = vk::raii::Buffer(device, vk::BufferCreateInfo({}, size, usage, vk::SharingMode::eExclusive));
        const vk::MemoryRequirements needs = buffer.getMemoryRequirements();
        memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(needs.size, memoryType(physical, needs.memoryTypeBits,
                                                                                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        buffer.bindMemory(*memory, 0);
        mapped = memory.mapMemory(0, size);
    }
};

struct Image {
    vk::raii::Image image{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    vk::raii::ImageView view{nullptr};

    Image() = default;
    Image(const vk::raii::Device& device, const vk::raii::PhysicalDevice& physical, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage,
          vk::ImageAspectFlags aspect) {
        image = vk::raii::Image(device, vk::ImageCreateInfo({}, vk::ImageType::e2D, format, vk::Extent3D(extent, 1), 1, 1, vk::SampleCountFlagBits::e1,
                                                            vk::ImageTiling::eOptimal, usage, vk::SharingMode::eExclusive, {}, vk::ImageLayout::eUndefined));
        const vk::MemoryRequirements needs = image.getMemoryRequirements();
        memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo(needs.size, memoryType(physical, needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
        image.bindMemory(*memory, 0);
        view = vk::raii::ImageView(device, vk::ImageViewCreateInfo({}, *image, vk::ImageViewType::e2D, format, {}, vk::ImageSubresourceRange(aspect, 0, 1, 0, 1)));
    }
};

// A render pass for one kind of picture, and the two ways of drawing into it.
struct Pass {
    vk::Format format = vk::Format::eUndefined;
    vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined;
    vk::raii::RenderPass renderPass{nullptr};
    vk::raii::Pipeline surfaces{nullptr};
    vk::raii::Pipeline lines{nullptr};
};

// Older Vulkan-Hpp gives a std::pair, newer a vk::ResultValue: either, as a pair.
template <class Given>
std::pair<vk::Result, std::uint32_t> resultAndValue(const Given& given) {
    if constexpr (requires { given.first; }) return {given.first, given.second};
    else return {given.result, given.value};
}

// The surface extensions a window may need, whichever system this is.
constexpr std::array windowExtensions{"VK_KHR_surface", "VK_KHR_win32_surface", "VK_KHR_xcb_surface", "VK_KHR_xlib_surface", "VK_KHR_wayland_surface", "VK_EXT_metal_surface"};

} // namespace

Colour Picture::at(int x, int y) const {
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
    return {pixels[i] / 255.0f, pixels[i + 1] / 255.0f, pixels[i + 2] / 255.0f, pixels[i + 3] / 255.0f};
}

struct Renderer::Inside {
    vk::raii::Context context;
    vk::raii::Instance instance{nullptr};
    vk::raii::PhysicalDevice physical{nullptr};
    vk::raii::Device device{nullptr};
    vk::raii::Queue queue{nullptr};
    std::uint32_t family = 0;
    std::string deviceName;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::raii::CommandPool pool{nullptr};
    vk::raii::CommandBuffer commands{nullptr};
    vk::raii::Fence done{nullptr};
    vk::raii::ShaderModule surfaceVertex{nullptr}, surfaceFragment{nullptr}, lineFragment{nullptr};
    vk::raii::PipelineLayout layout{nullptr};
    std::vector<Pass> passes;
    Buffer vertices, indices;

    // The picture read back by draw(), kept while its size stays.
    struct Offscreen {
        vk::Extent2D extent;
        Image colour, depth;
        vk::raii::Framebuffer framebuffer{nullptr};
        Buffer readBack;
    };
    std::optional<Offscreen> offscreen;

    const Pass& passFor(vk::Format format, vk::ImageLayout finalLayout);
    // Writes the scene into the buffers: what is drawn, and where in them.
    struct Draws {
        struct Indexed {
            std::uint32_t count, first;
            std::int32_t base;
            Colour colour;
            bool lit;
        };
        struct Plain {
            std::uint32_t count, first;
            Colour colour;
        };
        std::vector<Indexed> surfaces;
        std::vector<Plain> lines;
        std::vector<Indexed> overlay;
    };
    Draws upload(const Scene& scene);
    void record(const Scene& scene, const Draws& draws, const Pass& pass, vk::Framebuffer framebuffer, vk::Extent2D extent);
    void waitUntilDone() {
        if (device.waitForFences({*done}, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) throw std::runtime_error("The graphics card did not finish drawing.");
    }
};

Renderer::Renderer() : inside_(std::make_unique<Inside>()) {}
Renderer::~Renderer() {
    if (inside_ && *inside_->device) inside_->device.waitIdle();
}

const std::string& Renderer::deviceName() const { return inside_->deviceName; }
VkInstance_T* Renderer::instance() const { return static_cast<VkInstance>(*inside_->instance); }

std::expected<std::unique_ptr<Renderer>, std::string> Renderer::create(Options options) try {
    std::unique_ptr<Renderer> made(new Renderer());
    Inside& in = *made->inside_;

    std::vector<const char*> extensions;
    if (options.forWindows)
        for (const vk::ExtensionProperties& have : in.context.enumerateInstanceExtensionProperties())
            for (const char* wanted : windowExtensions)
                if (std::strcmp(have.extensionName, wanted) == 0) extensions.push_back(wanted);
    const vk::ApplicationInfo application("Cloth CAD", 1, "Cloth CAD", 1, VK_API_VERSION_1_1);
    in.instance = vk::raii::Instance(in.context, vk::InstanceCreateInfo({}, &application, {}, extensions));

    // The first card that draws; a real one before a software one.
    vk::raii::PhysicalDevices cards(in.instance);
    if (cards.empty()) return std::unexpected("Vulkan found no graphics card. Update the graphics driver.");
    std::stable_sort(cards.begin(), cards.end(), [](const vk::raii::PhysicalDevice& a, const vk::raii::PhysicalDevice& b) {
        const auto rank = [](const vk::raii::PhysicalDevice& card) {
            const vk::PhysicalDeviceType type = card.getProperties().deviceType;
            return type == vk::PhysicalDeviceType::eDiscreteGpu ? 0 : type == vk::PhysicalDeviceType::eIntegratedGpu ? 1 : 2;
        };
        return rank(a) < rank(b);
    });
    bool found = false;
    for (vk::raii::PhysicalDevice& card : cards) {
        const std::vector<vk::QueueFamilyProperties> families = card.getQueueFamilyProperties();
        for (std::uint32_t i = 0; i < families.size() && !found; ++i)
            if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                in.physical = std::move(card);
                in.family = i;
                found = true;
            }
        if (found) break;
    }
    if (!found) return std::unexpected("The graphics card cannot draw with Vulkan. Update the graphics driver.");
    in.deviceName = in.physical.getProperties().deviceName.data();

    for (const vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint, vk::Format::eD16Unorm})
        if (in.physical.getFormatProperties(format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            in.depthFormat = format;
            break;
        }

    std::vector<const char*> deviceExtensions;
    if (options.forWindows)
        for (const vk::ExtensionProperties& have : in.physical.enumerateDeviceExtensionProperties())
            if (std::strcmp(have.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    const float priority = 1.0f;
    const vk::DeviceQueueCreateInfo queue({}, in.family, 1, &priority);
    in.device = vk::raii::Device(in.physical, vk::DeviceCreateInfo({}, queue, {}, deviceExtensions));
    in.queue = vk::raii::Queue(in.device, in.family, 0);
    in.pool = vk::raii::CommandPool(in.device, vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, in.family));
    in.commands = std::move(vk::raii::CommandBuffers(in.device, vk::CommandBufferAllocateInfo(*in.pool, vk::CommandBufferLevel::ePrimary, 1)).front());
    in.done = vk::raii::Fence(in.device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));

    const auto module = [&](const auto& code) { return vk::raii::ShaderModule(in.device, vk::ShaderModuleCreateInfo({}, sizeof code, code)); };
    in.surfaceVertex = module(surfaceVertexCode);
    in.surfaceFragment = module(surfaceFragmentCode);
    in.lineFragment = module(lineFragmentCode);
    const vk::PushConstantRange push(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(Push));
    in.layout = vk::raii::PipelineLayout(in.device, vk::PipelineLayoutCreateInfo({}, {}, push));
    return made;
} catch (const std::exception& problem) {
    return std::unexpected(std::string("Vulkan could not start: ") + problem.what() + ". Update the graphics driver.");
}

const Pass& Renderer::Inside::passFor(vk::Format format, vk::ImageLayout finalLayout) {
    for (const Pass& pass : passes)
        if (pass.format == format && pass.finalLayout == finalLayout) return pass;
    Pass pass;
    pass.format = format;
    pass.finalLayout = finalLayout;
    const std::array attachments{
        vk::AttachmentDescription({}, format, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                  vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, finalLayout),
        vk::AttachmentDescription({}, depthFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                                  vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eDepthStencilAttachmentOptimal),
    };
    const vk::AttachmentReference colourAt(0, vk::ImageLayout::eColorAttachmentOptimal), depthAt(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    const vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colourAt, {}, &depthAt);
    // The picture's last use (shown, or copied out) waits for the drawing.
    const vk::SubpassDependency before(VK_SUBPASS_EXTERNAL, 0,
                                       vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
                                       vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests, {},
                                       vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
    pass.renderPass = vk::raii::RenderPass(device, vk::RenderPassCreateInfo({}, attachments, subpass, before));

    const vk::VertexInputBindingDescription binding(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
    const std::array inputs{vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)),
                            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal))};
    const vk::PipelineVertexInputStateCreateInfo vertexInput({}, binding, inputs);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    const vk::PipelineMultisampleStateCreateInfo samples({}, vk::SampleCountFlagBits::e1);
    const vk::PipelineDepthStencilStateCreateInfo depth({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);
    const vk::PipelineColorBlendAttachmentState blend(VK_TRUE, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                                                      vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                                                      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                                                          vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blending({}, VK_FALSE, vk::LogicOp::eCopy, blend);
    const std::array dynamic{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamic);

    const auto pipeline = [&](vk::PrimitiveTopology topology, const vk::raii::ShaderModule& fragment) {
        const std::array stages{vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *surfaceVertex, "main"),
                                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragment, "main")};
        const vk::PipelineInputAssemblyStateCreateInfo assembly({}, topology);
        // Cloth has two sides: nothing is culled.
        const vk::PipelineRasterizationStateCreateInfo raster({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
                                                              vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
        return vk::raii::Pipeline(device, nullptr,
                                  vk::GraphicsPipelineCreateInfo({}, stages, &vertexInput, &assembly, nullptr, &viewport, &raster, &samples, &depth, &blending,
                                                                 &dynamicState, *layout, *pass.renderPass, 0));
    };
    pass.surfaces = pipeline(vk::PrimitiveTopology::eTriangleList, surfaceFragment);
    pass.lines = pipeline(vk::PrimitiveTopology::eLineList, lineFragment);
    passes.push_back(std::move(pass));
    return passes.back();
}

Renderer::Inside::Draws Renderer::Inside::upload(const Scene& scene) {
    std::size_t vertexCount = 0, indexCount = 0;
    for (const Surface& s : scene.surfaces) vertexCount += s.points.size(), indexCount += s.triangles.size();
    for (const Surface& s : scene.overlay) vertexCount += s.points.size(), indexCount += s.triangles.size();
    for (const Lines& l : scene.lines) vertexCount += l.points.size();
    vertices.fit(device, physical, std::max<std::size_t>(vertexCount, 1) * sizeof(Vertex), vk::BufferUsageFlagBits::eVertexBuffer);
    indices.fit(device, physical, std::max<std::size_t>(indexCount, 1) * sizeof(std::uint32_t), vk::BufferUsageFlagBits::eIndexBuffer);

    Draws draws;
    auto* vertex = static_cast<Vertex*>(vertices.mapped);
    auto* index = static_cast<std::uint32_t*>(indices.mapped);
    std::uint32_t vertexAt = 0, indexAt = 0;
    const auto put = [&](const std::vector<Surface>& from, std::vector<Draws::Indexed>& into) {
        for (const Surface& s : from) {
            if (s.triangles.empty() || s.points.empty()) continue;
            for (std::size_t i = 0; i < s.points.size(); ++i) vertex[vertexAt + i] = {s.points[i], i < s.normals.size() ? s.normals[i] : Point{0, 0, 1}};
            std::copy(s.triangles.begin(), s.triangles.end(), index + indexAt);
            into.push_back({static_cast<std::uint32_t>(s.triangles.size()), indexAt, static_cast<std::int32_t>(vertexAt), s.colour, s.lit});
            vertexAt += static_cast<std::uint32_t>(s.points.size());
            indexAt += static_cast<std::uint32_t>(s.triangles.size());
        }
    };
    put(scene.surfaces, draws.surfaces);
    put(scene.overlay, draws.overlay);
    for (const Lines& l : scene.lines) {
        if (l.points.size() < 2) continue;
        for (std::size_t i = 0; i < l.points.size(); ++i) vertex[vertexAt + i] = {l.points[i], {}};
        draws.lines.push_back({static_cast<std::uint32_t>(l.points.size() / 2 * 2), vertexAt, l.colour});
        vertexAt += static_cast<std::uint32_t>(l.points.size());
    }
    return draws;
}

void Renderer::Inside::record(const Scene& scene, const Draws& draws, const Pass& pass, vk::Framebuffer framebuffer, vk::Extent2D extent) {
    const std::array clear{vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{scene.background.red, scene.background.green, scene.background.blue, 1.0f})),
                           vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))};
    commands.beginRenderPass(vk::RenderPassBeginInfo(*pass.renderPass, framebuffer, vk::Rect2D({0, 0}, extent), clear), vk::SubpassContents::eInline);
    commands.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
    commands.setScissor(0, vk::Rect2D({0, 0}, extent));
    commands.bindVertexBuffers(0, {*vertices.buffer}, {vk::DeviceSize(0)});
    commands.bindIndexBuffer(*indices.buffer, 0, vk::IndexType::eUint32);
    Push push{scene.viewProjection, {}, {scene.toLight.x, scene.toLight.y, scene.toLight.z, 0.0f}};
    const vk::ShaderStageFlags stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *pass.surfaces);
    // The light's w says whether a surface is lit (0) or drawn in its colour alone (1).
    for (const Draws::Indexed& d : draws.surfaces) {
        push.colour = d.colour;
        push.toLight[3] = d.lit ? 0.0f : 1.0f;
        commands.pushConstants<Push>(*layout, stages, 0, push);
        commands.drawIndexed(d.count, 1, d.first, d.base, 0);
    }
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *pass.lines);
    for (const Draws::Plain& d : draws.lines) {
        push.colour = d.colour;
        commands.pushConstants<Push>(*layout, stages, 0, push);
        commands.draw(d.count, 1, d.first, 0);
    }
    if (!draws.overlay.empty()) {
        // In front of everything drawn so far: the depth starts again, so the
        // overlay hides only its own parts behind each other.
        commands.clearAttachments(vk::ClearAttachment(vk::ImageAspectFlagBits::eDepth, 0, vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))),
                                  vk::ClearRect(vk::Rect2D({0, 0}, extent), 0, 1));
        commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *pass.surfaces);
        for (const Draws::Indexed& d : draws.overlay) {
            push.colour = d.colour;
            push.toLight[3] = d.lit ? 0.0f : 1.0f;
            commands.pushConstants<Push>(*layout, stages, 0, push);
            commands.drawIndexed(d.count, 1, d.first, d.base, 0);
        }
    }
    commands.endRenderPass();
}

std::expected<Picture, std::string> Renderer::draw(const Scene& scene, int width, int height) try {
    Inside& in = *inside_;
    if (width <= 0 || height <= 0) return Picture{std::max(width, 0), std::max(height, 0), {}};
    const vk::Extent2D extent(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    in.waitUntilDone();  // the buffers and the command buffer are free again
    const Pass& pass = in.passFor(pictureFormat, vk::ImageLayout::eTransferSrcOptimal);
    if (!in.offscreen || in.offscreen->extent != extent) {
        in.offscreen.reset();
        Inside::Offscreen o;
        o.extent = extent;
        o.colour = Image(in.device, in.physical, extent, pictureFormat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor);
        o.depth = Image(in.device, in.physical, extent, in.depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);
        const std::array views{*o.colour.view, *o.depth.view};
        o.framebuffer = vk::raii::Framebuffer(in.device, vk::FramebufferCreateInfo({}, *pass.renderPass, views, extent.width, extent.height, 1));
        o.readBack.fit(in.device, in.physical, vk::DeviceSize(extent.width) * extent.height * 4, vk::BufferUsageFlagBits::eTransferDst);
        in.offscreen = std::move(o);
    }
    const Inside::Draws draws = in.upload(scene);
    in.device.resetFences({*in.done});
    in.commands.reset();
    in.commands.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    in.record(scene, draws, pass, *in.offscreen->framebuffer, extent);
    in.commands.copyImageToBuffer(*in.offscreen->colour.image, vk::ImageLayout::eTransferSrcOptimal, *in.offscreen->readBack.buffer,
                                  vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), {0, 0, 0}, vk::Extent3D(extent, 1)));
    in.commands.end();
    const vk::CommandBuffer commandBuffer = *in.commands;
    in.queue.submit(vk::SubmitInfo({}, {}, commandBuffer), *in.done);
    in.waitUntilDone();
    Picture picture{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)};
    std::memcpy(picture.pixels.data(), in.offscreen->readBack.mapped, picture.pixels.size());
    return picture;
} catch (const std::exception& problem) {
    return std::unexpected(std::string("The 3D view could not be drawn: ") + problem.what());
}

// ---- Windows ---------------------------------------------------------------

struct Swapchain::Inside {
    Inside(Renderer::Inside& drawing, vk::SurfaceKHR window) : renderer(drawing), surface(window) {}
    Renderer::Inside& renderer;
    vk::SurfaceKHR surface;  // the toolkit's: not destroyed here
    vk::Format format = vk::Format::eUndefined;
    vk::Extent2D extent;
    vk::raii::SwapchainKHR swapchain{nullptr};
    std::vector<vk::Image> images;
    std::vector<vk::raii::ImageView> views;
    Image depth;
    std::vector<vk::raii::Framebuffer> framebuffers;
    vk::raii::Semaphore imageReady{nullptr};
    std::vector<vk::raii::Semaphore> drawn;  // one per image: it is free again once that image is shown
    bool stale = true;

    void make(vk::Extent2D wanted);
};

Swapchain::Swapchain(Renderer& renderer, VkSurfaceKHR_T* surface) : inside_(std::make_unique<Inside>(*renderer.inside_, vk::SurfaceKHR(surface))) {}

Swapchain::~Swapchain() {
    if (!inside_) return;
    inside_->renderer.device.waitIdle();
}

std::expected<std::unique_ptr<Swapchain>, std::string> Renderer::swapchainFor(VkSurfaceKHR_T* surface) try {
    if (!inside_->physical.getSurfaceSupportKHR(inside_->family, vk::SurfaceKHR(surface)))
        return std::unexpected("The graphics card cannot show pictures in this window. Update the graphics driver.");
    std::unique_ptr<Swapchain> made(new Swapchain(*this, surface));
    const std::vector<vk::SurfaceFormatKHR> formats = inside_->physical.getSurfaceFormatsKHR(vk::SurfaceKHR(surface));
    if (formats.empty()) return std::unexpected("The window offers no picture format to draw in.");
    made->inside_->format = formats.front().format == vk::Format::eUndefined ? vk::Format::eB8G8R8A8Unorm : formats.front().format;
    // Colours are given as they are to be seen: a plain (not sRGB) format keeps them so.
    for (const vk::SurfaceFormatKHR& f : formats)
        if (f.format == vk::Format::eB8G8R8A8Unorm || f.format == vk::Format::eR8G8B8A8Unorm) made->inside_->format = f.format;
    made->inside_->imageReady = vk::raii::Semaphore(inside_->device, vk::SemaphoreCreateInfo());
    return made;
} catch (const std::exception& problem) {
    return std::unexpected(std::string("The 3D view could not use the window: ") + problem.what());
}

void Swapchain::Inside::make(vk::Extent2D wanted) {
    renderer.device.waitIdle();
    framebuffers.clear();
    views.clear();
    drawn.clear();
    const vk::SurfaceCapabilitiesKHR can = renderer.physical.getSurfaceCapabilitiesKHR(surface);
    extent = can.currentExtent.width != UINT32_MAX ? can.currentExtent
                                                   : vk::Extent2D(std::clamp(wanted.width, can.minImageExtent.width, can.maxImageExtent.width),
                                                                  std::clamp(wanted.height, can.minImageExtent.height, can.maxImageExtent.height));
    if (extent.width == 0 || extent.height == 0) return;  // minimised: nothing to draw into
    std::uint32_t count = can.minImageCount + 1;
    if (can.maxImageCount > 0) count = std::min(count, can.maxImageCount);
    vk::CompositeAlphaFlagBitsKHR alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    for (const vk::CompositeAlphaFlagBitsKHR a : {vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eInherit,
                                                  vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied})
        if (can.supportedCompositeAlpha & a) {
            alpha = a;
            break;
        }
    vk::SwapchainCreateInfoKHR info({}, surface, count, format, vk::ColorSpaceKHR::eSrgbNonlinear, extent, 1, vk::ImageUsageFlagBits::eColorAttachment,
                                    vk::SharingMode::eExclusive, {}, can.currentTransform, alpha, vk::PresentModeKHR::eFifo, VK_TRUE, *swapchain);
    vk::raii::SwapchainKHR next(renderer.device, info);
    swapchain = std::move(next);
    images.clear();
    for (const auto image : swapchain.getImages()) images.push_back(vk::Image(image));
    const Pass& pass = renderer.passFor(format, vk::ImageLayout::ePresentSrcKHR);
    depth = Image(renderer.device, renderer.physical, extent, renderer.depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);
    for (const vk::Image image : images) {
        views.emplace_back(renderer.device, vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, format, {},
                                                                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        const std::array attached{*views.back(), *depth.view};
        framebuffers.emplace_back(renderer.device, vk::FramebufferCreateInfo({}, *pass.renderPass, attached, extent.width, extent.height, 1));
        drawn.emplace_back(renderer.device, vk::SemaphoreCreateInfo());
    }
    stale = false;
}

std::expected<void, std::string> Swapchain::present(const Scene& scene, int width, int height) try {
    Inside& in = *inside_;
    Renderer::Inside& r = in.renderer;
    if (width <= 0 || height <= 0) return {};
    const vk::Extent2D wanted(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    r.waitUntilDone();
    if (in.stale || !*in.swapchain || in.extent != wanted) in.make(wanted);
    if (!*in.swapchain || in.framebuffers.empty()) return {};

    std::uint32_t image = 0;
    try {
        const auto [result, index] = resultAndValue(in.swapchain.acquireNextImage(UINT64_MAX, *in.imageReady));
        image = index;
        if (result == vk::Result::eSuboptimalKHR) in.stale = true;  // shown anyway; made again next time
    } catch (const vk::OutOfDateKHRError&) {
        in.stale = true;
        return {};
    }
    const Pass& pass = r.passFor(in.format, vk::ImageLayout::ePresentSrcKHR);
    const Renderer::Inside::Draws draws = r.upload(scene);
    r.device.resetFences({*r.done});
    r.commands.reset();
    r.commands.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    r.record(scene, draws, pass, *in.framebuffers[image], in.extent);
    r.commands.end();
    const vk::PipelineStageFlags waitAt = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    const vk::Semaphore ready = *in.imageReady, finished = *in.drawn[image];
    const vk::CommandBuffer commandBuffer = *r.commands;
    r.queue.submit(vk::SubmitInfo(ready, waitAt, commandBuffer, finished), *r.done);
    try {
        const vk::SwapchainKHR chain = *in.swapchain;
        if (r.queue.presentKHR(vk::PresentInfoKHR(finished, chain, image)) == vk::Result::eSuboptimalKHR) in.stale = true;
    } catch (const vk::OutOfDateKHRError&) {
        in.stale = true;
    }
    // The next acquire signals the same semaphore: wait until this one was used.
    r.waitUntilDone();
    return {};
} catch (const std::exception& problem) {
    inside_->stale = true;
    return std::unexpected(std::string("The 3D view could not be shown: ") + problem.what());
}

} // namespace clothcad::render
