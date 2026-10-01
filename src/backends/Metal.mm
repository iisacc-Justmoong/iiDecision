#include "Backend.h"
#import <Metal/Metal.h>
#include <TargetConditionals.h>
#include <algorithm>
#include <limits>

namespace iiDecision::detail {
namespace {
const char* source = R"(
#include <metal_stdlib>
using namespace metal;
kernel void evidence_match(device const uint* values [[buffer(0)]],
    device const uint* query [[buffer(1)]], device uint* mask [[buffer(2)]],
    constant uint2& shape [[buffer(3)]], uint r [[thread_position_in_grid]]) {
    if (r >= shape.x) return;
    uint matched = 1;
    for (uint c = 0; c < shape.y; ++c)
        if (values[r * shape.y + c] != query[c]) { matched = 0; break; }
    mask[r] = matched;
})";
class Metal final : public IEvidenceBackend {
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLComputePipelineState> pipeline_;
    BackendInfo info_;
public:
    Metal(id<MTLDevice> device, std::size_t index) : device_(device) {
        info_ = {"metal/"+std::to_string(index),"Metal",device.name.UTF8String,DeviceKind::Gpu,true,{}};
        queue_ = [device_ newCommandQueue];
        if (!queue_) throw std::runtime_error("Metal command queue creation failed");
        NSError* error = nil;
        auto library = [device_ newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&error];
        if (!library) throw std::runtime_error(error.localizedDescription.UTF8String);
        auto function = [library newFunctionWithName:@"evidence_match"];
        pipeline_ = [device_ newComputePipelineStateWithFunction:function error:&error];
        if (!pipeline_) throw std::runtime_error(error.localizedDescription.UTF8String);
    }
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) override {
        @autoreleasepool {
            if (rows > UINT32_MAX || evidence.size() > UINT32_MAX || values.size() > UINT32_MAX)
                throw std::length_error("Metal evidence matrix exceeds uint32 addressing");
            auto matrix = [device_ newBufferWithBytes:values.data() length:values.size_bytes() options:MTLResourceStorageModeShared];
            auto query = [device_ newBufferWithBytes:evidence.data() length:evidence.size_bytes() options:MTLResourceStorageModeShared];
            auto output = [device_ newBufferWithLength:rows*sizeof(std::uint32_t) options:MTLResourceStorageModeShared];
            if (!matrix || !query || !output) throw std::runtime_error("Metal buffer allocation failed");
            auto command = [queue_ commandBuffer];
            auto encoder = [command computeCommandEncoder];
            if (!command || !encoder) throw std::runtime_error("Metal command allocation failed");
            [encoder setComputePipelineState:pipeline_];
            [encoder setBuffer:matrix offset:0 atIndex:0];
            [encoder setBuffer:query offset:0 atIndex:1];
            [encoder setBuffer:output offset:0 atIndex:2];
            std::uint32_t shape[]{static_cast<std::uint32_t>(rows),static_cast<std::uint32_t>(evidence.size())};
            [encoder setBytes:shape length:sizeof(shape) atIndex:3];
            const auto width = std::min<NSUInteger>(pipeline_.maxTotalThreadsPerThreadgroup,256);
            [encoder dispatchThreads:MTLSizeMake(rows,1,1) threadsPerThreadgroup:MTLSizeMake(width,1,1)];
            [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted)
                throw std::runtime_error(command.error ? command.error.localizedDescription.UTF8String : "Metal execution failed");
            const auto* data = static_cast<const std::uint32_t*>(output.contents);
            std::vector<std::uint8_t> result(rows);
            for (std::size_t r = 0; r < rows; ++r) {
                if (data[r] > 1) throw std::runtime_error("Metal returned a nonbinary mask");
                result[r] = static_cast<std::uint8_t>(data[r]);
            }
            return result;
        }
    }
};
}
void addMetal(BackendList& result) {
    @autoreleasepool {
        std::size_t index = 0;
#if TARGET_OS_OSX
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
#else
        auto device = MTLCreateSystemDefaultDevice();
        NSArray<id<MTLDevice>>* devices = device ? @[device] : @[];
#endif
        for (id<MTLDevice> device in devices) {
            try { result.push_back(std::make_shared<Metal>(device,index)); }
            catch (const std::exception& error) { result.push_back(std::make_shared<Unavailable>("metal/"+std::to_string(index),"Metal",error.what())); }
            ++index;
        }
        if (!index) result.push_back(std::make_shared<Unavailable>("metal","Metal","no Metal device detected"));
    }
}
}
