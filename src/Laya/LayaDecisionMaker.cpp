// Compatibility boundary for the retired learned Laya model backend.
// (Convai Innovations, Apache-2.0). See third_party/laya/NOTICE and LICENSE.
#include <iiDecision/LayaDecisionMaker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>


namespace iiDecision {
namespace {
bool blank(std::string_view text) {
    return text.empty() || text.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos;
}
void validate(const LayaQuestion& q) {
    if(blank(q.id) || blank(q.instructions)) throw std::invalid_argument("Laya question needs an ID and instructions");
    if(q.type!=LayaQuestionType::Choice && q.type!=LayaQuestionType::Score && q.type!=LayaQuestionType::Noul)
        throw std::invalid_argument("unknown Laya question type");
    if(q.options.empty() || q.options.size()>65536) throw std::invalid_argument("Laya question needs 1..65536 options");
    std::set<std::string> ids;
    for(std::size_t i=0;i<q.options.size();++i) {
        const auto& id=q.options[i].id;
        if(id.empty() || !ids.insert(id).second) throw std::invalid_argument("Laya option IDs must be nonempty and unique");
        if(q.type==LayaQuestionType::Score && id!=std::to_string(i))
            throw std::invalid_argument("Laya score option IDs must be consecutive level indices");
    }
    if(q.type==LayaQuestionType::Noul && (q.options.size()!=2 || q.options[0].id!="false" || q.options[1].id!="true"))
        throw std::invalid_argument("Laya noul requires false, true in semantic order");
}
std::string quote(std::string_view value) {
    static constexpr char hex[]="0123456789abcdef";
    std::string result="\"";
    for(const auto raw:value) {
        const auto c=static_cast<unsigned char>(raw);
        if(c=='"' || c=='\\') { result+='\\'; result+=static_cast<char>(c); }
        else if(c=='\b') result+="\\b";
        else if(c=='\f') result+="\\f";
        else if(c=='\n') result+="\\n";
        else if(c=='\r') result+="\\r";
        else if(c=='\t') result+="\\t";
        else if(c<0x20) { result+="\\u00"; result+=hex[c>>4]; result+=hex[c&15]; }
        else result+=static_cast<char>(c);
    }
    return result+'"';
}
}

struct LayaDecisionMaker::Impl { LayaOptions options; };
bool LayaDecisionMaker::available() noexcept { return false; }

LayaDecisionMaker::LayaDecisionMaker(const std::filesystem::path& package,LayaOptions options) {
    (void)package; (void)options;
    throw std::runtime_error("The learned Laya model backend has been retired; use the explicit decision interfaces");
}
LayaDecisionMaker::~LayaDecisionMaker()=default;
std::vector<LayaAnswer> LayaDecisionMaker::predict(std::string_view state,std::span<const LayaQuestion> questions) const {
    (void)state; (void)questions; throw std::runtime_error("Laya backend unavailable");
}
LayaAnswer LayaDecisionMaker::evaluate(std::string_view state,std::span<const Candidate> candidates) const {
    LayaQuestion q{"action",impl_->options.instructions,LayaQuestionType::Choice,{}};
    for(const auto& candidate:candidates) q.options.push_back({candidate.id,{}});
    validate(q);
    return predict(state,std::span<const LayaQuestion>(&q,1)).front();
}
Decision LayaDecisionMaker::decide(const Context& context,std::span<const Candidate> candidates) {
    std::string state="{"; std::set<std::string> keys;
    for(const auto& observation:context.observations) {
        if(observation.key.empty() || !keys.insert(observation.key).second) throw std::invalid_argument("Laya Context keys must be nonempty and unique");
        if(state.size()>1) state+=", ";
        state+=quote(observation.key)+": "+quote(observation.value);
    }
    state+='}';
    return evaluate(state,candidates).decision;
}
} // namespace iiDecision
