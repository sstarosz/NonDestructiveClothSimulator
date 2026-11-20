#pragma once
// Draws a Scene with Vulkan (through Vulkan-Hpp), either into a picture that
// is read back (tests, thumbnails) or into a window's swapchain. Vulkan is
// loaded when the program runs, so nothing needs linking; on a machine with
// no Vulkan, create() says why and the window shows that instead.
//
//   std::expected<std::unique_ptr<render::Renderer>, std::string> made = render::Renderer::create({});
//   if (made) {
//       std::expected<render::Picture, std::string> picture = (*made)->draw(scene, 640, 480);
//   }
//
// Nothing here throws: problems come back as words.

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "clothcad/render/Scene.h"

// Vulkan's handles, without its headers.
struct VkInstance_T;
struct VkSurfaceKHR_T;

namespace clothcad::render {

// A picture read back from the graphics card: rows from the top, four bytes
// a pixel (red, green, blue, alpha).
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    Colour at(int x, int y) const;
};

class Swapchain;

class Renderer {
public:
    struct Options {
        // Turn on what drawing into windows needs: surfaces and swapchains.
        bool forWindows = false;
    };
    static std::expected<std::unique_ptr<Renderer>, std::string> create(Options options);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // The graphics card it draws with, for people to read: "llvmpipe (LLVM 15.0.7, 256 bits)".
    const std::string& deviceName() const;
    // Vulkan's instance, for a toolkit that makes the surface of a window.
    VkInstance_T* instance() const;

    std::expected<Picture, std::string> draw(const Scene& scene, int width, int height);

    // Draws into a window's surface, which the toolkit made with instance()
    // and keeps. The swapchain must go before the surface and the renderer.
    std::expected<std::unique_ptr<Swapchain>, std::string> swapchainFor(VkSurfaceKHR_T* surface);

private:
    Renderer();
    friend class Swapchain;
    struct Inside;
    std::unique_ptr<Inside> inside_;
};

// A window's pictures: each present() draws one and shows it. A window of a
// new size gets a new swapchain by itself.
class Swapchain {
public:
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    // Empty when it was shown, or could not be this time (the window was
    // being resized or hidden); words when something went wrong.
    std::expected<void, std::string> present(const Scene& scene, int width, int height);

private:
    friend class Renderer;
    explicit Swapchain(Renderer& renderer, VkSurfaceKHR_T* surface);
    struct Inside;
    std::unique_ptr<Inside> inside_;
};

} // namespace clothcad::render
