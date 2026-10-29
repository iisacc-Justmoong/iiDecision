#include <iiDecision/Acceleration.h>
#include "GpuAdapter.h"
#include <array>
#include <iostream>

int main(int argc,char** argv) {
    if(argc!=2) return 2;
    try {
        const std::string prefix=argv[1];
        auto backends=iiDecision::gpu_adapter::discover(iiDecision::gpu_adapter::GpuApi::metal);
        auto vulkan=iiDecision::gpu_adapter::discover(iiDecision::gpu_adapter::GpuApi::vulkan);
        backends.insert(backends.end(),vulkan.begin(),vulkan.end());
        iiDecision::EvidenceMatcher discovery({},backends);
        for(const auto& backend:discovery.backends()) {
            if(!backend.available||backend.kind==iiDecision::DeviceKind::Cpu||!backend.id.starts_with(prefix)) continue;
            iiDecision::EvidenceMatcher matcher({.mode=iiDecision::ExecutionMode::RequireAccelerators,.backend_ids={backend.id}},backends);
            const std::array<std::uint32_t,6> values{UINT32_MAX,16777217,UINT32_MAX-1,16777217,UINT32_MAX,16777218};
            const std::array<std::uint32_t,2> query{UINT32_MAX,16777217};
            auto result=matcher.match(3,values,query);
            if(result.mask!=std::vector<std::uint8_t>{1,0,0}||result.execution.cpu_rows!=0||
                result.execution.records.size()!=1||!result.execution.records[0].completed) return 3;
            std::cout<<"INSTALLED_GPU "<<backend.id<<" exact=true cpu_rows=0\n"; return 0;
        }
        return 77;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
