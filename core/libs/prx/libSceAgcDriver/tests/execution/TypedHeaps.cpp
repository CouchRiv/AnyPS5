#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

alignas(256) std::array<std::array<std::uint32_t, 64>, 4> texels{};
alignas(256) std::array<std::array<std::uint32_t, 64>, 4> outputs{};
constexpr std::array<std::uint32_t, 8> code{0x7e3c0300u, 0x7e3e0280u, 0xf0001108u, 0x00010a1eu, 0x34060082u, 0xe0701000u, 0x80000a03u, 0xbf810000u};
constexpr std::array<std::uint32_t, 10> storeCode{0x7e3c0300u, 0x7e3e0280u, 0x7e1402ffu, 123u, 0xf0201108u, 0x00010a1eu, 0x34060082u, 0xe0701000u, 0x80000a03u, 0xbf810000u};
constexpr std::uint32_t WideImages = 40u;
alignas(256) std::array<std::array<std::uint32_t, 64>, WideImages> wideTexels{};
std::array<std::array<std::uint32_t, 8>, WideImages> wideDescriptors{};
std::array<std::uint32_t, WideImages * 32u> wideOutput{};

ShaderRecompiler::RecompileResult Compile(AgcDriver::VulkanDevice& device, std::uint32_t index, bool nullImage, bool store) {
    const auto output = reinterpret_cast<std::uintptr_t>(outputs[index].data());
    const auto image = reinterpret_cast<std::uintptr_t>(texels[index].data());
    std::array<std::uint32_t, 12> userData{static_cast<std::uint32_t>(output), static_cast<std::uint32_t>(output >> 32u), 256u, 0x31016facu};
    if (!nullImage) {
        const std::array<std::uint32_t, 8> descriptor{static_cast<std::uint32_t>(image >> 8u), static_cast<std::uint32_t>(image >> 40u) | (20u << 20u) | (3u << 30u), 7u, 0x90000facu, 0u, 0u, 0u, 0u};
        std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 4u);
    }
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    const auto instructions = store ? std::span<const std::uint32_t>(storeCode) : std::span<const std::uint32_t>(code);
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(instructions.data()), instructions, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, {}}, device.Target(), {0u, 0u, 0u, 128u}};
    auto result = ShaderRecompiler::Recompile(request);
    Require(result.runtimeImageCount == 0u, "direct image allocated runtime image metadata");
    Require(result.shaderDataDwords < ShaderRecompiler::RuntimeAbi::ShaderDataDwords, "shader data was not compacted");
    for (const auto& binding : result.bindings) {
        if (binding.role == ShaderRecompiler::DescriptorRole::GuestImages) Require(binding.count == 1u, "single image expanded into unused descriptor slots");
    }
    return result;
}

void Run(AgcDriver::VulkanDevice& device) {
    std::vector<ShaderRecompiler::RecompileResult> shaders;
    for (std::uint32_t batch = 0u; batch < outputs.size(); ++batch) {
        outputs[batch].fill(0xdeadbeefu);
        for (std::uint32_t pixel = 0u; pixel < 32u; ++pixel) texels[batch][pixel] = 1000u * (batch + 1u) + pixel;
        const bool nullImage = batch >= 2u;
        shaders.push_back(Compile(device, batch, nullImage, batch == 3u));
        device.Dispatch(shaders.back(), 1u, 1u, 1u);
        device.SubmitRecorded();
    }
    shaders.clear();
    device.WaitIdle();
    for (std::uint32_t batch = 0u; batch < outputs.size(); ++batch) {
        for (std::uint32_t pixel = 0u; pixel < 32u; ++pixel) Require(outputs[batch][pixel] == (batch == 3u ? 123u : batch == 2u ? 0u : texels[batch][pixel]), "descriptor heap lifetime or null image semantics failed: batch=" + std::to_string(batch) + " pixel=" + std::to_string(pixel) + " value=" + std::to_string(outputs[batch][pixel]));
    }
}

void RunWide(AgcDriver::VulkanDevice& device) {
    std::vector<std::uint32_t> code{0x7e3c0300u, 0x7e3e0280u, 0x34060082u};
    for (std::uint32_t image = 0u; image < WideImages; ++image) {
        const auto address = reinterpret_cast<std::uintptr_t>(wideTexels[image].data());
        for (std::uint32_t pixel = 0u; pixel < 32u; ++pixel) wideTexels[image][pixel] = 1000u * (image + 1u) + pixel;
        wideDescriptors[image] = {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u) | (20u << 20u) | (3u << 30u), 7u, 0x90000facu, 0u, 0u, 0u, 0u};
        code.insert(code.end(), {0xf40c0202u, 0xfa000000u | (image * 32u), 0xbf8cc07fu, 0xf0001108u, 0x00020a1eu, 0x4a0806ffu, image * 128u, 0xbf8c3f70u, 0xe0701000u, 0x80000a04u});
    }
    code.push_back(0xbf810000u);
    wideOutput.fill(0xdeadbeefu);
    const auto output = reinterpret_cast<std::uintptr_t>(wideOutput.data());
    const auto table = reinterpret_cast<std::uintptr_t>(wideDescriptors.data());
    const std::array<std::uint32_t, 6> userData{static_cast<std::uint32_t>(output), static_cast<std::uint32_t>(output >> 32u), static_cast<std::uint32_t>(sizeof(wideOutput)), 0x31016facu, static_cast<std::uint32_t>(table), static_cast<std::uint32_t>(table >> 32u)};
    const std::array regions{ShaderRecompiler::MemoryRegion{table, std::as_bytes(std::span(wideDescriptors))}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, regions}, device.Target(), {0u, 0u, 0u, 128u}};
    const auto shader = ShaderRecompiler::Recompile(request);
    const auto images = std::ranges::count_if(shader.bindings, [](const auto& binding) { return binding.role == ShaderRecompiler::DescriptorRole::GuestImages && binding.count == WideImages; });
    Require(images == 1, "distinct images of one class were not placed in one heap");
    device.Dispatch(shader, 1u, 1u, 1u);
    device.SubmitRecorded();
    device.WaitIdle();
    for (std::uint32_t image = 0u; image < WideImages; ++image) {
        for (std::uint32_t pixel = 0u; pixel < 32u; ++pixel) Require(wideOutput[image * 32u + pixel] == wideTexels[image][pixel], "wide typed heap read the wrong image: image=" + std::to_string(image) + " pixel=" + std::to_string(pixel) + " value=" + std::to_string(wideOutput[image * 32u + pixel]));
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Run(*device);
        RunWide(*device);
        std::cout << "typed heap execution tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
