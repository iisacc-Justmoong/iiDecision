// Sequence construction and decoding adapted from Laya common.py / onnx_agent.py
// (Convai Innovations, Apache-2.0). See third_party/laya/NOTICE and LICENSE.
#include <iiDecision/LayaDecisionMaker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

#ifdef IIDECISION_HAS_LAYA
#include "laya_tokenizer/Bridge.h"
#include <onnxruntime_cxx_api.h>
#endif

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

#ifdef IIDECISION_HAS_LAYA
namespace {
std::string nativeError(char* error) {
    std::unique_ptr<char,decltype(&iid_laya_error_free)> owned(error,iid_laya_error_free);
    return error ? error : "Laya tokenizer failed";
}
std::string utf8(const std::filesystem::path& path) {
    const auto s=path.u8string(); return {s.begin(),s.end()};
}
struct Encoded {
    std::vector<std::int64_t> ids,markers;
    std::size_t state_tokens=0,state_dropped=0,distinct=0;
};
constexpr const char* inputNames[]{"input_ids","attention_mask","marker_pos","marker_mask","qtype"};
constexpr const char* outputNames[]{"logits","act_logits"};
}
struct LayaDecisionMaker::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR,"iiDecision Laya"};
    std::unique_ptr<void,decltype(&iid_laya_close)> tokenizer{nullptr,iid_laya_close};
    std::unique_ptr<Ort::Session> session;
    LayaOptions options;
    std::array<std::uint32_t,4> special{};
    std::size_t max_length=0,head_length=0;

    Impl(const std::filesystem::path& package,LayaOptions settings):options(std::move(settings)) {
        if(options.intra_op_threads<1 || blank(options.instructions))
            throw std::invalid_argument("Laya needs positive thread count and nonempty decision instructions");
        char* error=nullptr;
        const auto path=utf8(package);
        if(path.find('\0')!=std::string::npos) throw std::invalid_argument("Laya package path contains NUL");
        tokenizer.reset(iid_laya_open(path.c_str(),&error));
        if(!tokenizer) throw std::runtime_error(nativeError(error));
        std::array<std::size_t,2> budgets{};
        iid_laya_config(tokenizer.get(),budgets.data(),special.data());
        max_length=options.max_length ? options.max_length : budgets[0];
        head_length=options.head_max_length ? options.head_max_length : budgets[1];
        if(max_length<1 || max_length>65536 || head_length<1 || head_length>65536)
            throw std::invalid_argument("Laya token budgets must be in [1,65536]");
        const auto graph=package/"laya.onnx";
        if(!std::filesystem::is_regular_file(graph)) throw std::runtime_error("Laya package is missing laya.onnx");
        try {
            Ort::SessionOptions cfg;
            cfg.SetIntraOpNumThreads(options.intra_op_threads);
            cfg.SetInterOpNumThreads(1);
            cfg.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
            // Default ORT sessions use the local CPU provider. No remote provider or silent model substitution.
            session=std::make_unique<Ort::Session>(env,graph.c_str(),cfg);
            checkSignature();
        } catch(const Ort::Exception& e) { throw std::runtime_error(std::string("Laya ONNX load: ")+e.what()); }
    }
    void checkSignature() const {
        if(session->GetInputCount()!=5 || session->GetOutputCount()!=2)
            throw std::runtime_error("Laya ONNX must expose the official five inputs and two outputs");
        Ort::AllocatorWithDefaultOptions allocator;
        const ONNXTensorElementDataType types[]{ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL,ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64};
        std::set<std::string> seen;
        for(std::size_t i=0;i<5;++i) {
            const auto name=session->GetInputNameAllocated(i,allocator);
            const auto it=std::find_if(std::begin(inputNames),std::end(inputNames),[&](const char* n){return std::string_view(n)==name.get();});
            if(it==std::end(inputNames) || !seen.insert(name.get()).second) throw std::runtime_error("unexpected Laya input name");
            const auto index=static_cast<std::size_t>(it-std::begin(inputNames));
            const auto type=session->GetInputTypeInfo(i);
            const auto info=type.GetTensorTypeAndShapeInfo();
            if(info.GetElementType()!=types[index] || info.GetShape().size()!=(index==4 ? 1u:2u))
                throw std::runtime_error("unexpected Laya input type or rank");
        }
        seen.clear();
        for(std::size_t i=0;i<2;++i) {
            const auto name=session->GetOutputNameAllocated(i,allocator);
            if((std::string_view(name.get())!="logits" && std::string_view(name.get())!="act_logits") || !seen.insert(name.get()).second)
                throw std::runtime_error("unexpected Laya output name");
            const auto type=session->GetOutputTypeInfo(i);
            const auto info=type.GetTensorTypeAndShapeInfo();
            if(info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetShape().size()!=2)
                throw std::runtime_error("Laya outputs must be rank-two float32 tensors");
        }
    }
    std::vector<std::int64_t> encode(std::string_view text) const {
        const std::string owned(text);
        std::uint32_t* raw=nullptr; std::size_t count=0; char* error=nullptr;
        if(iid_laya_encode(tokenizer.get(),reinterpret_cast<const unsigned char*>(owned.data()),owned.size(),&raw,&count,&error)!=0)
            throw std::invalid_argument(nativeError(error));
        const auto deleter=[count](std::uint32_t* p){iid_laya_tokens_free(p,count);};
        std::unique_ptr<std::uint32_t,decltype(deleter)> tokens(raw,deleter);
        if(count==0) return {};
        return {raw,raw+count};
    }
    Encoded assemble(std::span<const std::int64_t> state,const LayaQuestion& q) const {
        const char* types[]{"choice","score","noul"};
        auto head=encode(std::string(types[static_cast<int>(q.type)])+" question: "+q.instructions);
        std::vector<std::vector<std::int64_t>> options;
        std::size_t total=0;
        for(std::size_t i=0;i<q.options.size();++i) {
            const auto& o=q.options[i]; std::string text;
            if(q.type==LayaQuestionType::Score) text="level "+std::to_string(i)+": "+o.description;
            else if(q.type==LayaQuestionType::Noul) text=o.id+": "+(o.description.empty() ?
                (i==0 ? "no, the statement does not hold":"yes, the statement holds") : o.description);
            else text=o.id+(o.description.empty() ? "":": "+o.description);
            auto tokens=encode(" "+text); if(tokens.size()>48) tokens.resize(48);
            tokens.insert(tokens.begin(),special[3]); total+=tokens.size(); options.push_back(std::move(tokens));
        }
        auto budget=static_cast<std::int64_t>(head_length)-static_cast<std::int64_t>(total);
        if(budget<16) {
            const auto per=std::max<std::int64_t>(4,(static_cast<std::int64_t>(head_length)-16)/static_cast<std::int64_t>(options.size()));
            total=0;
            for(auto& o:options) { if(o.size()>static_cast<std::size_t>(per)) o.resize(static_cast<std::size_t>(per)); total+=o.size(); }
            budget=static_cast<std::int64_t>(head_length)-static_cast<std::int64_t>(total);
        }
        const auto cap=static_cast<std::size_t>(std::max<std::int64_t>(8,budget));
        if(head.size()>cap) head.resize(cap);
        Encoded row; row.ids.push_back(special[0]); row.ids.insert(row.ids.end(),head.begin(),head.end()); row.ids.push_back(special[1]);
        std::set<std::vector<std::int64_t>> distinct;
        for(const auto& o:options) {
            distinct.insert(o); row.markers.push_back(static_cast<std::int64_t>(row.ids.size())); row.ids.insert(row.ids.end(),o.begin(),o.end());
        }
        row.ids.push_back(special[1]);
        const auto room=max_length>row.ids.size() ? max_length-row.ids.size()-1:0;
        const auto used=std::min(room,state.size());
        row.ids.insert(row.ids.end(),state.begin(),state.begin()+used); row.ids.push_back(special[1]);
        if(row.ids.size()>max_length) row.ids.resize(max_length);
        if(std::any_of(row.markers.begin(),row.markers.end(),[&](auto m){return static_cast<std::size_t>(m)>=max_length;}))
            throw std::invalid_argument("Laya option markers exceed max_length; increase the token budget or reduce options");
        row.state_tokens=state.size(); row.state_dropped=state.size()-used; row.distinct=distinct.size();
        return row;
    }
    std::vector<LayaAnswer> predict(std::string_view state,std::span<const LayaQuestion> questions) const {
        if(questions.empty()) return {};
        if(questions.size()>65536) throw std::invalid_argument("Laya question batch exceeds 65536 rows");
        std::set<std::string> question_ids;
        for(const auto& q:questions) { validate(q); if(!question_ids.insert(q.id).second) throw std::invalid_argument("duplicate Laya question ID"); }
        const auto state_ids=encode(state);
        std::vector<Encoded> rows;
        std::size_t length=0,markers=0;
        for(const auto& q:questions) {
            rows.push_back(assemble(state_ids,q)); length=std::max(length,rows.back().ids.size()); markers=std::max(markers,q.options.size());
        }
        // The official ONNX export traces the act head's topk(2) branch. A masked
        // second slot preserves eager single-option semantics for that graph.
        markers=std::max<std::size_t>(2,markers);
        const auto batch=questions.size();
        if(batch>std::numeric_limits<std::size_t>::max()/std::max(length,markers)) throw std::length_error("Laya tensor size overflow");
        std::vector<std::int64_t> ids(batch*length,special[2]),attention(batch*length,0),positions(batch*markers,0),qtype(batch);
        auto masks=std::make_unique<bool[]>(batch*markers);
        for(std::size_t r=0;r<batch;++r) {
            std::copy(rows[r].ids.begin(),rows[r].ids.end(),ids.begin()+r*length);
            std::fill_n(attention.begin()+r*length,rows[r].ids.size(),1);
            std::copy(rows[r].markers.begin(),rows[r].markers.end(),positions.begin()+r*markers);
            for(std::size_t m=0;m<markers;++m) masks[r*markers+m]=m<rows[r].markers.size();
            qtype[r]=static_cast<int>(questions[r].type);
        }
        const std::int64_t token_shape[]{static_cast<std::int64_t>(batch),static_cast<std::int64_t>(length)};
        const std::int64_t marker_shape[]{token_shape[0],static_cast<std::int64_t>(markers)};
        const std::int64_t batch_shape[]{token_shape[0]};
        const auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        Ort::Value inputs[]{Ort::Value::CreateTensor<std::int64_t>(memory,ids.data(),ids.size(),token_shape,2),
            Ort::Value::CreateTensor<std::int64_t>(memory,attention.data(),attention.size(),token_shape,2),
            Ort::Value::CreateTensor<std::int64_t>(memory,positions.data(),positions.size(),marker_shape,2),
            Ort::Value::CreateTensor<bool>(memory,masks.get(),batch*markers,marker_shape,2),
            Ort::Value::CreateTensor<std::int64_t>(memory,qtype.data(),qtype.size(),batch_shape,1)};
        std::vector<Ort::Value> outputs;
        try { outputs=session->Run(Ort::RunOptions{nullptr},inputNames,inputs,5,outputNames,2); }
        catch(const Ort::Exception& e) { throw std::runtime_error(std::string("Laya ONNX inference: ")+e.what()); }
        for(const auto& out:outputs) if(!out.IsTensor() || out.GetTensorTypeAndShapeInfo().GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("Laya returned a non-float tensor");
        const auto logits_shape=outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const auto act_shape=outputs[1].GetTensorTypeAndShapeInfo().GetShape();
        if(logits_shape!=std::vector<std::int64_t>{token_shape[0],marker_shape[1]} || act_shape.size()!=2 || act_shape[0]!=token_shape[0] || act_shape[1]<1)
            throw std::runtime_error("Laya returned invalid logits dimensions");
        const auto* logits=outputs[0].GetTensorData<float>(); const auto* act=outputs[1].GetTensorData<float>();
        std::vector<LayaAnswer> results;
        for(std::size_t r=0;r<batch;++r) {
            const auto& q=questions[r]; const auto k=q.options.size();
            LayaAnswer answer; answer.question_id=q.id; answer.type=q.type;
            answer.temperature=iid_laya_temperature(tokenizer.get(),static_cast<std::uint32_t>(q.type),k);
            long double maximum=-std::numeric_limits<long double>::infinity(); std::size_t best=0;
            for(std::size_t i=0;i<k;++i) {
                if(!std::isfinite(logits[r*markers+i])) throw std::runtime_error("Laya returned nonfinite logits");
                const auto z=static_cast<long double>(logits[r*markers+i])/answer.temperature;
                if(z>maximum) {maximum=z; best=i;}
            }
            long double sum=0;
            for(std::size_t i=0;i<k;++i) sum+=std::exp(static_cast<long double>(logits[r*markers+i])/answer.temperature-maximum);
            const auto log_sum=std::log(sum);
            long double entropy=0;
            for(std::size_t i=0;i<k;++i) {
                const auto log_p=static_cast<long double>(logits[r*markers+i])/answer.temperature-maximum-log_sum;
                const auto p=std::exp(log_p);
                answer.probabilities.push_back({q.options[i].id,p,log_p});
                // Match upstream confidence's probability clipping without rounding the public distribution.
                entropy-=p*std::log(std::max(1e-12L,p)); answer.score+=static_cast<long double>(i)*p;
            }
            answer.decision={q.options[best].id}; answer.answer_confidence=answer.probabilities[best].probability;
            answer.confidence=k<2 ? 1:std::clamp(1-entropy/std::log(static_cast<long double>(k)),0.L,1.L);
            if(q.type==LayaQuestionType::Noul) {answer.noul=answer.probabilities[1].probability; answer.confidence=answer.answer_confidence;}
            if(q.type!=LayaQuestionType::Score) answer.score=0;
            const auto act_count=static_cast<std::size_t>(act_shape[1]); long double act_max=-std::numeric_limits<long double>::infinity();
            for(std::size_t i=0;i<act_count;++i) {
                if(!std::isfinite(act[r*act_count+i])) throw std::runtime_error("Laya returned nonfinite action logits");
                act_max=std::max(act_max,static_cast<long double>(act[r*act_count+i]));
            }
            long double act_sum=0;
            for(std::size_t i=0;i<act_count;++i) act_sum+=std::exp(static_cast<long double>(act[r*act_count+i])-act_max);
            answer.act_probability=std::exp(static_cast<long double>(act[r*act_count])-act_max)/act_sum;
            answer.input_tokens=rows[r].ids.size(); answer.state_tokens=rows[r].state_tokens;
            answer.state_tokens_dropped=rows[r].state_dropped; answer.options_distinct=rows[r].distinct;
            answer.execution.cpu_rows=batch; answer.execution.records.push_back({"laya/onnx/CPUExecutionProvider",0,batch,true,{}});
            results.push_back(std::move(answer));
        }
        return results;
    }
};
bool LayaDecisionMaker::available() noexcept { return true; }
#else
struct LayaDecisionMaker::Impl { LayaOptions options; };
bool LayaDecisionMaker::available() noexcept { return false; }
#endif

LayaDecisionMaker::LayaDecisionMaker(const std::filesystem::path& package,LayaOptions options) {
#ifdef IIDECISION_HAS_LAYA
    impl_=std::make_unique<Impl>(package,std::move(options));
#else
    (void)package; (void)options;
    throw std::runtime_error("iiDecision was built without the Laya backend; enable IIDECISION_LAYA with ONNX Runtime and Cargo");
#endif
}
LayaDecisionMaker::~LayaDecisionMaker()=default;
std::vector<LayaAnswer> LayaDecisionMaker::predict(std::string_view state,std::span<const LayaQuestion> questions) const {
#ifdef IIDECISION_HAS_LAYA
    return impl_->predict(state,questions);
#else
    (void)state; (void)questions; throw std::runtime_error("Laya backend unavailable");
#endif
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
