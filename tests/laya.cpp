#include <iiDecision/LayaDecisionMaker.h>
#include <iiDecision/Acceleration.h>
#include <iostream>
#include <stdexcept>
int main() {
    try {
        if (iiDecision::LayaDecisionMaker::available()) throw std::runtime_error("Retired model advertised as available");
        bool rejected=false;
        try { iiDecision::LayaDecisionMaker model("unused-package"); }
        catch (const std::runtime_error &error) { rejected=std::string(error.what()).find("retired")!=std::string::npos; }
        if (!rejected) throw std::runtime_error("Retired model request did not fail explicitly");
        const iiDecision::EvidenceMatcher matcher;
        for (const auto &backend : matcher.backends())
            if (backend.id.starts_with("onnx/") || backend.api.find("ONNX")!=std::string::npos)
                throw std::runtime_error("Retired runtime was discovered");
        std::cout << "Retired model boundary and native backend inventory passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
