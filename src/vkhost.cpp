// vkhost — the Stage 6 Vulkan host (BUILD-PLAN pieces 2–5, 7, 8).
//
// One binary, subcommands:
//   selftest  CPU-side checks that need no GPU: SHA-256 vectors, the
//             ownership transition graph, the ternary-dot CPU
//             reference. Also validates any .spv named on the command
//             line through the SPIR-V gate.
//   compute   The first workload: the ternary dot-product kernel,
//             dispatched over host-visible buffers. The GPU result
//             must equal the CPU reference exactly (integer gate).
//   render    The render half: an offscreen frame rendered into a
//             host buffer and handed off with its hashes. No
//             swapchain, no display code — presentation is
//             PureBasic's (owner's ruling).
//   all       compute, then render.
//
// Every SPIR-V module enters through the gate (SPIR-V Tools
// validation) and is registered by SHA-256 before a pipeline exists.
// Every buffer the GPU touches moves through the ownership table.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

#ifdef VKHOST_WITH_GATE
#include <spirv-tools/libspirv.hpp>
#endif

#include "ownership.h"
#include "sha256.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

// ---------------------------------------------------------------- gate

bool spvGate(const std::vector<uint32_t>& words, const std::string& name) {
#ifdef VKHOST_WITH_GATE
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_1);
    if (!tools.IsValid()) {
        std::fprintf(stderr, "vkhost: gate unavailable\n");
        return false;
    }
    std::string messages;
    tools.SetMessageConsumer([&messages](spv_message_level_t, const char*,
                                         const spv_position_t& pos, const char* msg) {
        messages += "line " + std::to_string(pos.index) + ": " + msg + "\n";
    });
    bool ok = tools.Validate(words.data(), words.size());
    if (!ok) std::fprintf(stderr, "vkhost: gate REFUSED %s\n%s", name.c_str(), messages.c_str());
    return ok;
#else
    (void)words; (void)name;
    std::fprintf(stderr, "vkhost: built without the SPIR-V gate — refusing to run modules\n");
    return false;
#endif
}

// Module registry: SHA-256 -> admitted module name. A module is
// admitted once, by hash, only after the gate passes.
std::map<std::string, std::string> g_registry;

std::vector<uint32_t> loadAndAdmit(const std::string& path, const std::string& name) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "vkhost: cannot open %s\n", path.c_str()); return {}; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() % 4 != 0) {
        std::fprintf(stderr, "vkhost: %s is not a SPIR-V module (size %zu)\n", path.c_str(), bytes.size());
        return {};
    }
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    std::string hash = sha256Hex(bytes.data(), bytes.size());
    if (g_registry.count(hash)) {
        std::printf("vkhost: module %s already admitted as %s\n", name.c_str(), hash.substr(0, 16).c_str());
        return words;
    }
    if (!spvGate(words, name)) return {};
    g_registry[hash] = name;
    std::printf("vkhost: gate ADMITTED %-16s sha256 %s\n", name.c_str(), hash.c_str());
    return words;
}

uint64_t fnv1a(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

// Deterministic input generator (splitmix64) — the same inputs on
// every run, on every machine, so the gate compares like with like.
struct SplitMix {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

// The CPU reference for the ternary dot kernel: plain integer
// accumulation of activation * weight, weights in {-1, 0, +1} —
// the semantics of the owner's Pascal reference kernel.
int32_t ternaryDotRef(const int32_t* a, const int32_t* w, uint32_t n) {
    int64_t acc = 0;
    for (uint32_t i = 0; i < n; ++i) acc += int64_t(a[i]) * int64_t(w[i]);
    return int32_t(acc);
}

// ---------------------------------------------------------------- GPU

struct Gpu {
    vk::Instance instance;
    vk::PhysicalDevice phys;
    vk::Device device;
    vk::Queue queue;
    uint32_t queueFamily = 0;
    vk::CommandPool pool;

    bool init() {
        vk::detail::DynamicLoader loader;
        PFN_vkGetInstanceProcAddr getAddr = nullptr;
        try {
            getAddr = loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        } catch (...) {
            std::fprintf(stderr, "vkhost: libvulkan loader not found\n");
            return false;
        }
        VULKAN_HPP_DEFAULT_DISPATCHER.init(getAddr);
        vk::ApplicationInfo appInfo("vkhost", 1, "jetson-vulkan-host", 1, VK_API_VERSION_1_1);
        try {
            instance = vk::createInstance({{}, &appInfo});
        } catch (const std::exception& e) {
            std::fprintf(stderr, "vkhost: Vulkan driver/ICD unavailable (%s)\n", e.what());
            return false;
        }
        VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
        auto devices = instance.enumeratePhysicalDevices();
        if (devices.empty()) { std::fprintf(stderr, "vkhost: no physical devices\n"); return false; }
        for (auto cand : devices) {
            auto queues = cand.getQueueFamilyProperties();
            for (uint32_t i = 0; i < queues.size(); ++i) {
                if (queues[i].queueFlags & vk::QueueFlagBits::eCompute) {
                    phys = cand; queueFamily = i;
                    auto props = cand.getProperties();
                    std::printf("vkhost: device %s (queue family %u)\n", props.deviceName.data(), i);
                    float prio = 1.0f;
                    vk::DeviceQueueCreateInfo qinfo({}, queueFamily, 1, &prio);
                    device = phys.createDevice({{}, 1, &qinfo});
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
                    queue = device.getQueue(queueFamily, 0);
                    pool = device.createCommandPool({vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamily});
                    return true;
                }
            }
        }
        std::fprintf(stderr, "vkhost: no compute queue family\n");
        return false;
    }

    uint32_t memoryType(vk::MemoryPropertyFlags want) const {
        auto mem = phys.getMemoryProperties();
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if ((mem.memoryTypes[i].propertyFlags & want) == want) return i;
        return UINT32_MAX;
    }

    struct Buffer {
        vk::Buffer buf;
        vk::DeviceMemory mem;
        void* mapped = nullptr;
        size_t bytes = 0;
    };

    // Host-visible, host-coherent buffer. On the Jetson's unified
    // memory this is also device-local in practice; elsewhere it is
    // the staging path. Either way the CPU and GPU read the same
    // bytes with no copy commands — ownership is the table's job.
    Buffer makeBuffer(size_t bytes, vk::BufferUsageFlags usage) {
        Buffer out;
        out.bytes = bytes;
        out.buf = device.createBuffer({{}, bytes, usage, vk::SharingMode::eExclusive});
        auto req = device.getBufferMemoryRequirements(out.buf);
        uint32_t type = memoryType(vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);
        if (type == UINT32_MAX) throw std::runtime_error("no host-visible coherent memory type");
        out.mem = device.allocateMemory({req.size, type});
        device.bindBufferMemory(out.buf, out.mem, 0);
        out.mapped = device.mapMemory(out.mem, 0, bytes);
        return out;
    }

    void freeBuffer(Buffer& b) {
        if (b.mapped) device.unmapMemory(b.mem);
        device.destroyBuffer(b.buf);
        device.freeMemory(b.mem);
        b = Buffer{};
    }

    void runCommands(const std::function<void(vk::CommandBuffer)>& record) {
        auto bufs = device.allocateCommandBuffers({pool, vk::CommandBufferLevel::ePrimary, 1});
        vk::CommandBuffer cmd = bufs[0];
        cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        record(cmd);
        cmd.end();
        vk::Fence fence = device.createFence({});
        vk::SubmitInfo submit({}, {}, {}, 1, &cmd);
        if (queue.submit(1, &submit, fence) != vk::Result::eSuccess)
            throw std::runtime_error("queue submit failed");
        auto wait = device.waitForFences(1, &fence, VK_TRUE, 60ull * 1000 * 1000 * 1000);
        if (wait != vk::Result::eSuccess) throw std::runtime_error("fence wait failed");
        device.destroyFence(fence);
        device.freeCommandBuffers(pool, bufs);
    }
};

// ---------------------------------------------------------------- compute

int runCompute(const std::string& spvPath) {
    Gpu gpu;
    if (!gpu.init()) return 3;
    auto words = loadAndAdmit(spvPath, "ternary_dot");
    if (words.empty()) return 4;

    const uint32_t n = 257;        // dot length — odd on purpose
    const uint32_t outputs = 96;
    std::vector<int32_t> a(size_t(outputs) * n), w(n), expect(outputs);
    SplitMix rng{0x5eedULL};
    for (auto& v : a) v = int32_t(rng.next() % 33) - 16;         // [-16, 16]
    for (auto& v : w) v = int32_t(rng.next() % 3) - 1;           // {-1, 0, +1}
    for (uint32_t o = 0; o < outputs; ++o) expect[o] = ternaryDotRef(&a[size_t(o) * n], w.data(), n);

    OwnershipTable table;
    uint32_t idA = table.addBuffer("activations", a.size() * 4);
    uint32_t idW = table.addBuffer("weights", w.size() * 4);
    uint32_t idO = table.addBuffer("outputs", expect.size() * 4);

    auto bufA = gpu.makeBuffer(a.size() * 4, vk::BufferUsageFlagBits::eStorageBuffer);
    auto bufW = gpu.makeBuffer(w.size() * 4, vk::BufferUsageFlagBits::eStorageBuffer);
    auto bufO = gpu.makeBuffer(expect.size() * 4, vk::BufferUsageFlagBits::eStorageBuffer);

    auto stage = [&](uint32_t id, const char* note) {
        bool ok = table.transition(id, Owner::Host, BufState::HostWriting, note) &&
                  table.transition(id, Owner::Host, BufState::HostReady, note);
        if (!ok) throw std::runtime_error("ownership: staging transition refused");
    };
    stage(idA, "fill activations"); stage(idW, "fill weights"); stage(idO, "zero outputs");
    std::memcpy(bufA.mapped, a.data(), a.size() * 4);
    std::memcpy(bufW.mapped, w.data(), w.size() * 4);
    std::memset(bufO.mapped, 0, expect.size() * 4);

    vk::ShaderModule module = gpu.device.createShaderModule({{}, words.size() * 4,
        reinterpret_cast<const uint32_t*>(words.data())});

    vk::DescriptorSetLayoutBinding binds[3] = {
        {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute}};
    vk::DescriptorSetLayout setLayout = gpu.device.createDescriptorSetLayout({{}, 3, binds});
    vk::PushConstantRange pcr(vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t));
    vk::PipelineLayout pipeLayout = gpu.device.createPipelineLayout({{}, 1, &setLayout, 1, &pcr});
    vk::ComputePipelineCreateInfo cpinfo({}, {{}, vk::ShaderStageFlagBits::eCompute, module, "main"}, pipeLayout);
    auto pipeResult = gpu.device.createComputePipeline(nullptr, cpinfo);
    if (pipeResult.result != vk::Result::eSuccess) throw std::runtime_error("compute pipeline creation failed");
    vk::Pipeline pipeline = pipeResult.value;

    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 3);
    vk::DescriptorPool dpool = gpu.device.createDescriptorPool({vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, 1, &poolSize});
    vk::DescriptorSet dset = gpu.device.allocateDescriptorSets({dpool, 1, &setLayout})[0];
    vk::DescriptorBufferInfo infos[3] = {
        {bufA.buf, 0, VK_WHOLE_SIZE}, {bufW.buf, 0, VK_WHOLE_SIZE}, {bufO.buf, 0, VK_WHOLE_SIZE}};
    vk::WriteDescriptorSet writes[3] = {
        {dset, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &infos[0]},
        {dset, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &infos[1]},
        {dset, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &infos[2]}};
    gpu.device.updateDescriptorSets(3, writes, 0, nullptr);

    for (uint32_t id : {idA, idW, idO})
        if (!table.transition(id, Owner::Gpu, BufState::GpuInFlight, "dispatch ternary_dot"))
            throw std::runtime_error("ownership: GPU transfer refused");
    gpu.runCommands([&](vk::CommandBuffer cmd) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeLayout, 0, 1, &dset, 0, nullptr);
        cmd.pushConstants(pipeLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t), &n);
        cmd.dispatch((outputs + 63) / 64, 1, 1);
    });
    for (uint32_t id : {idA, idW, idO}) {
        if (!table.transition(id, Owner::Gpu, BufState::GpuReady, "fence signalled")) throw std::runtime_error("ownership: GPU-ready refused");
        if (!table.transition(id, Owner::Host, BufState::HostReady, "readback")) throw std::runtime_error("ownership: readback refused");
    }

    const int32_t* got = static_cast<const int32_t*>(bufO.mapped);
    uint32_t mismatches = 0, firstBad = 0;
    for (uint32_t o = 0; o < outputs; ++o)
        if (got[o] != expect[o]) { if (!mismatches) firstBad = o; ++mismatches; }
    std::string outHash = sha256Hex(bufO.mapped, expect.size() * 4);
    std::printf("vkhost: compute n=%u outputs=%u mismatches=%u", n, outputs, mismatches);
    if (mismatches) std::printf(" (first at %u: gpu=%d cpu=%d)", firstBad, got[firstBad], expect[firstBad]);
    std::printf("\nvkhost: outputs sha256 %s\n", outHash.c_str());
    std::printf("vkhost: compute gate %s — GPU equals CPU reference %s\n",
                mismatches == 0 ? "PASS" : "FAIL", mismatches == 0 ? "exactly" : "NOT");

    std::printf("vkhost: ownership log (%zu transfers)\n", table.log().size());
    for (const auto& t : table.log())
        std::printf("  buffer %u: %s -> %s [%s] %s\n", t.bufferId, toString(t.from),
                    toString(t.to), toString(t.resultingState), t.note.c_str());

    gpu.device.destroyPipeline(pipeline);
    gpu.device.destroyPipelineLayout(pipeLayout);
    gpu.device.destroyDescriptorPool(dpool);
    gpu.device.destroyDescriptorSetLayout(setLayout);
    gpu.device.destroyShaderModule(module);
    gpu.freeBuffer(bufA); gpu.freeBuffer(bufW); gpu.freeBuffer(bufO);
    return mismatches == 0 ? 0 : 5;
}

// ---------------------------------------------------------------- render

int runRender(const std::string& vertPath, const std::string& fragPath, const std::string& outDir) {
    Gpu gpu;
    if (!gpu.init()) return 3;
    auto vertWords = loadAndAdmit(vertPath, "fullscreen.vert");
    auto fragWords = loadAndAdmit(fragPath, "gradient.frag");
    if (vertWords.empty() || fragWords.empty()) return 4;

    const uint32_t W = 320, H = 240;
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    vk::Image image = gpu.device.createImage({{}, vk::ImageType::e2D, format,
        {W, H, 1}, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive});
    auto imgReq = gpu.device.getImageMemoryRequirements(image);
    uint32_t imgType = gpu.memoryType(vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::DeviceMemory imgMem = gpu.device.allocateMemory({imgReq.size, imgType});
    gpu.device.bindImageMemory(image, imgMem, 0);
    vk::ImageView view = gpu.device.createImageView({{}, image, vk::ImageViewType::e2D, format,
        {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});

    vk::AttachmentDescription att({}, format, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);
    vk::AttachmentReference ref(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &ref);
    vk::RenderPass renderPass = gpu.device.createRenderPass({{}, 1, &att, 1, &subpass});
    vk::Framebuffer fb = gpu.device.createFramebuffer({{}, renderPass, 1, &view, W, H, 1});

    vk::ShaderModule vertMod = gpu.device.createShaderModule({{}, vertWords.size() * 4, vertWords.data()});
    vk::ShaderModule fragMod = gpu.device.createShaderModule({{}, fragWords.size() * 4, fragWords.data()});
    vk::PipelineShaderStageCreateInfo stages[2] = {
        {{}, vk::ShaderStageFlagBits::eVertex, vertMod, "main"},
        {{}, vk::ShaderStageFlagBits::eFragment, fragMod, "main"}};
    vk::PipelineVertexInputStateCreateInfo vin;
    vk::PipelineInputAssemblyStateCreateInfo ia({}, vk::PrimitiveTopology::eTriangleList);
    vk::Viewport vp(0, 0, float(W), float(H), 0, 1);
    vk::Rect2D sc({0, 0}, {W, H});
    vk::PipelineViewportStateCreateInfo vps({}, 1, &vp, 1, &sc);
    vk::PipelineRasterizationStateCreateInfo rast({}, VK_FALSE, VK_FALSE,
        vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0, 0, 0, 1.0f);
    vk::PipelineMultisampleStateCreateInfo ms({}, vk::SampleCountFlagBits::e1);
    vk::ColorComponentFlags allcm = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                    vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendAttachmentState blendAtt(VK_FALSE, {}, {}, {}, {}, {}, {}, allcm);
    vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1, &blendAtt);
    vk::PushConstantRange pcr(vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(float));
    vk::PipelineLayout pipeLayout = gpu.device.createPipelineLayout({{}, 0, nullptr, 1, &pcr});
    auto pipeResult = gpu.device.createGraphicsPipeline(nullptr, {{}, 2, stages, &vin, &ia, nullptr,
        &vps, &rast, &ms, nullptr, &blend, nullptr, pipeLayout, renderPass, 0});
    if (pipeResult.result != vk::Result::eSuccess) throw std::runtime_error("graphics pipeline creation failed");
    vk::Pipeline pipeline = pipeResult.value;

    OwnershipTable table;
    uint32_t idFb = table.addBuffer("frame-readback", size_t(W) * H * 4);
    auto readback = gpu.makeBuffer(size_t(W) * H * 4, vk::BufferUsageFlagBits::eTransferDst);
    if (!table.transition(idFb, Owner::Host, BufState::HostWriting, "prepare readback") ||
        !table.transition(idFb, Owner::Host, BufState::HostReady, "readback armed"))
        throw std::runtime_error("ownership: readback staging refused");
    if (!table.transition(idFb, Owner::Gpu, BufState::GpuInFlight, "render + copy"))
        throw std::runtime_error("ownership: GPU transfer refused");

    float dims[2] = {float(W), float(H)};
    vk::ClearValue clear(std::array<float, 4>{0.f, 0.f, 0.f, 1.f});
    gpu.runCommands([&](vk::CommandBuffer cmd) {
        cmd.beginRenderPass({renderPass, fb, {{0, 0}, {W, H}}, 1, &clear}, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmd.pushConstants(pipeLayout, vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(float), dims);
        cmd.draw(3, 1, 0, 0);
        cmd.endRenderPass();
        vk::BufferImageCopy region({}, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {W, H, 1});
        cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, readback.buf, 1, &region);
    });
    if (!table.transition(idFb, Owner::Gpu, BufState::GpuReady, "fence signalled") ||
        !table.transition(idFb, Owner::Display, BufState::HostReady, "handoff to PureBasic"))
        throw std::runtime_error("ownership: handoff refused");

    std::string pixHash = sha256Hex(readback.mapped, size_t(W) * H * 4);
    uint64_t pixFnv = fnv1a(readback.mapped, size_t(W) * H * 4);
    std::string rgbaPath = outDir + "/frame.rgba";
    std::string handPath = outDir + "/frame.handoff";
    { std::ofstream f(rgbaPath, std::ios::binary); f.write(static_cast<const char*>(readback.mapped), size_t(W) * H * 4); }
    { std::ofstream f(handPath);
      f << "frame: " << W << "x" << H << " RGBA8\n"
        << "sha256: " << pixHash << "\n"
        << "fnv1a64: " << std::hex << pixFnv << std::dec << "\n"
        << "bytes: " << size_t(W) * H * 4 << "\n"
        << "presentation: PureBasic reads frame.rgba; this host ships no swapchain\n"; }
    std::printf("vkhost: rendered %ux%u offscreen; frame sha256 %s\n", W, H, pixHash.c_str());
    std::printf("vkhost: handoff written: %s + %s\n", rgbaPath.c_str(), handPath.c_str());

    gpu.device.destroyPipeline(pipeline);
    gpu.device.destroyPipelineLayout(pipeLayout);
    gpu.device.destroyShaderModule(vertMod);
    gpu.device.destroyShaderModule(fragMod);
    gpu.device.destroyFramebuffer(fb);
    gpu.device.destroyRenderPass(renderPass);
    gpu.device.destroyImageView(view);
    gpu.device.destroyImage(image);
    gpu.device.freeMemory(imgMem);
    gpu.freeBuffer(readback);
    return 0;
}

// ---------------------------------------------------------------- selftest

int runSelftest(const std::vector<std::string>& spvFiles) {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("vkhost: selftest %-42s %s\n", what.c_str(), ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    };
    check(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256 empty vector");
    const char* abc = "abc";
    check(sha256Hex(abc, 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256 abc vector");
    const char* fips = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(sha256Hex(fips, std::strlen(fips)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 56-byte vector");

    OwnershipTable t;
    uint32_t id = t.addBuffer("probe", 64);
    check(!t.transition(id, Owner::Gpu, BufState::GpuInFlight, "skip states"), "ownership refuses state skip");
    check(t.transition(id, Owner::Host, BufState::HostWriting, "write") &&
          t.transition(id, Owner::Host, BufState::HostReady, "seal") &&
          t.transition(id, Owner::Gpu, BufState::GpuInFlight, "dispatch") &&
          t.transition(id, Owner::Gpu, BufState::GpuReady, "done") &&
          t.transition(id, Owner::Host, BufState::HostReady, "readback"),
          "ownership legal path");

    const int32_t a[5] = {3, -2, 7, 0, 5};
    const int32_t w[5] = {1, -1, 0, 1, -1};
    check(ternaryDotRef(a, w, 5) == 0, "ternary dot reference (3+2+0+0-5=0)");
    const int32_t a2[3] = {4, 4, 4};
    const int32_t w2[3] = {1, 1, 1};
    check(ternaryDotRef(a2, w2, 3) == 12, "ternary dot reference (4+4+4=12)");

    for (const auto& path : spvFiles) {
        auto words = loadAndAdmit(path, path);
        check(!words.empty(), "gate admits " + path);
    }
    std::printf("vkhost: selftest %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string shaderDir = "shaders";
    std::string cmd = argc > 1 ? argv[1] : "selftest";
    try {
        if (cmd == "selftest") {
            std::vector<std::string> spv;
            for (int i = 2; i < argc; ++i) spv.push_back(argv[i]);
            return runSelftest(spv);
        }
        if (cmd == "compute") return runCompute(shaderDir + "/ternary_dot.comp.spv");
        if (cmd == "render") return runRender(shaderDir + "/fullscreen.vert.spv",
                                              shaderDir + "/gradient.frag.spv", ".");
        if (cmd == "all") {
            int rc = runCompute(shaderDir + "/ternary_dot.comp.spv");
            if (rc != 0) return rc;
            return runRender(shaderDir + "/fullscreen.vert.spv",
                             shaderDir + "/gradient.frag.spv", ".");
        }
        std::fprintf(stderr, "usage: vkhost [selftest [module.spv ...] | compute | render | all]\n");
        return 64;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "vkhost: fatal: %s\n", e.what());
        return 70;
    }
}
