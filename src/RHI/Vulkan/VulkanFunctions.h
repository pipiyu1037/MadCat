#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <vector>

namespace madcat::rhi::detail
{
inline bool checkResult(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    std::fprintf(stderr, "Vulkan error: %s failed (VkResult %d).\n", operation, static_cast<int>(result));
    return false;
}

template<typename Function>
Function loadFunction(VkInstance instance, const char* name)
{
    const auto function = reinterpret_cast<Function>(glfwGetInstanceProcAddress(instance, name));
    if (!function)
    {
        std::fprintf(stderr, "Vulkan error: required function %s is unavailable.\n", name);
    }
    return function;
}

template<typename Property, typename Enumerate>
bool enumerateProperties(Enumerate enumerate, const char* operation, std::vector<Property>& properties)
{
    VkResult result;
    do
    {
        uint32_t count = 0;
        if (!checkResult(enumerate(&count, nullptr), operation))
        {
            return false;
        }
        properties.resize(count);
        if (count == 0)
        {
            return true;
        }
        result = enumerate(&count, properties.data());
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        {
            return checkResult(result, operation);
        }
        properties.resize(count);
    } while (result == VK_INCOMPLETE);
    return checkResult(result, operation);
}
}
