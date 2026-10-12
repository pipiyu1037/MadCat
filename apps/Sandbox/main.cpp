#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanInstance.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

int main()
{
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW error (%d): %s\n", code, description);
    });

    if (glfwInit() != GLFW_TRUE)
    {
        return EXIT_FAILURE;
    }

    // Vulkan will provide rendering later; this window needs no OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "MadCat", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_FAILURE;
    try
    {
        madcat::rhi::VulkanInstance vulkan;
        if (vulkan.initialize())
        {
            while (!glfwWindowShouldClose(window))
            {
                glfwWaitEvents();
            }
            exitCode = EXIT_SUCCESS;
        }
        // The Vulkan owner is destroyed before GLFW unloads the Vulkan loader.
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "MadCat initialization failed: %s\n", error.what());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
