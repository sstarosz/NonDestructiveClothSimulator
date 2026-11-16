#pragma once
// The window of 0.1 run in tests, with no window or graphics card.

#include <imgui.h>

// Dear ImGui set up as the window would have it, without a window: frames are
// built and thrown away. One per test case.
struct ImGuiForTests {
    explicit ImGuiForTests(ImVec2 display = ImVec2(1500, 900), float textScale = 1.0f) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // as the OpenGL backend has
        io.DisplaySize = display;
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::GetStyle().FontScaleMain = textScale;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~ImGuiForTests() { ImGui::DestroyContext(); }
    ImGuiForTests(const ImGuiForTests&) = delete;
    ImGuiForTests& operator=(const ImGuiForTests&) = delete;
};
