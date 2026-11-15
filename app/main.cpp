// The window: a GLFW window with an OpenGL context, drawn with Dear ImGui.
// Everything about cloth is in App; this file only opens the window and
// runs the frame loop.

#include <cstdio>
#include <filesystem>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include "app/App.h"

int main() {
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
    });
    if (!glfwInit()) return 1;

    // On a high-resolution display the system reports a scale such as 1.5 or
    // 2. The window, the font and every size in the interface follow it.
    const float displayScale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1500, 900, "Non-Destructive Cloth CAD 0.1", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // wait for the display's refresh

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no layout file written next to the program
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ScaleAllSizes(displayScale);
    style.FontScaleDpi = displayScale;
    style.FontSizeBase = 17.0f;

    // A system font drawn at the real pixel size, in place of ImGui's small
    // built-in bitmap font. The first one found is used.
    for (const char* font : {"C:\\Windows\\Fonts\\segoeui.ttf",
                             "/System/Library/Fonts/Supplemental/Arial.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
        if (std::filesystem::exists(font)) {
            ImGui::GetIO().Fonts->AddFontFromFileTTF(font);
            break;
        }
    }
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        clothcad::app::App app;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();

            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
