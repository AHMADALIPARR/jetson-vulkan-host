// device_probe — BUILD-PLAN piece 1.
// Opens the GPU through the Vulkan loader and prints its properties
// and queue families. Proves the driver path before anything is
// built on it. Exit codes: 0 = a device with a compute queue was
// found; 2 = loader missing; 3 = no suitable device.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

#include <cstdio>
#include <cstring>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

int main() try {
    vk::detail::DynamicLoader loader;
    PFN_vkGetInstanceProcAddr getAddr = nullptr;
    try {
        getAddr = loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    } catch (...) {
        std::fprintf(stderr, "probe: libvulkan loader not found\n");
        return 2;
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(getAddr);

    vk::ApplicationInfo appInfo("device-probe", 1, "jetson-vulkan-host", 1, VK_API_VERSION_1_1);
    vk::Instance instance = vk::createInstance({{}, &appInfo});
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        std::fprintf(stderr, "probe: no physical devices\n");
        return 3;
    }

    int suitable = 0;
    for (auto dev : devices) {
        auto props = dev.getProperties();
        auto mem = dev.getMemoryProperties();
        std::printf("device: %s\n", props.deviceName.data());
        std::printf("  type: %s  api: %u.%u.%u  driver: 0x%08x  vendor: 0x%04x\n",
                    vk::to_string(props.deviceType).c_str(),
                    VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                    VK_VERSION_PATCH(props.apiVersion), props.driverVersion, props.vendorID);
        uint64_t heaps = 0;
        for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) heaps += mem.memoryHeaps[i].size;
        std::printf("  memory heaps total: %llu MiB across %u heap(s)\n",
                    (unsigned long long)(heaps / (1024 * 1024)), mem.memoryHeapCount);
        auto queues = dev.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queues.size(); ++i) {
            const auto& q = queues[i];
            std::printf("  queue family %u: count=%u flags=%s%s%s%s\n", i, q.queueCount,
                        (q.queueFlags & vk::QueueFlagBits::eGraphics) ? " graphics" : "",
                        (q.queueFlags & vk::QueueFlagBits::eCompute) ? " compute" : "",
                        (q.queueFlags & vk::QueueFlagBits::eTransfer) ? " transfer" : "",
                        (q.queueFlags & vk::QueueFlagBits::eSparseBinding) ? " sparse" : "");
            if (q.queueFlags & vk::QueueFlagBits::eCompute) suitable++;
        }
    }
    std::printf("probe: %zu device(s), %d compute-capable queue family(ies)\n",
                devices.size(), suitable);
    return suitable > 0 ? 0 : 3;
} catch (const std::exception& e) {
    std::fprintf(stderr, "probe: Vulkan driver/ICD unavailable (%s)\n", e.what());
    return 3;
}
