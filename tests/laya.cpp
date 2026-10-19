#include <iiDecision/LayaDecisionMaker.h>
#include "backends/OnnxModel.h"
#ifdef IIDECISION_TEST_ORT_API_VERSION
#include <onnxruntime_c_api.h>
#endif

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace iiDecision;
using iiDecision::detail::onnx::Message;
using iiDecision::detail::onnx::node;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class E, class F> void rejects(F f) {
    try { f(); } catch (const E&) { return; }
    throw std::runtime_error("expected exception");
}
Message value(std::string_view name, unsigned type, std::initializer_list<std::string_view> dims) {
    Message shape;
    for (auto dim : dims) { Message d; d.string(2, dim); shape.message(1, d); }
    Message tensor; tensor.integer(1, type); tensor.message(2, shape);
    Message t; t.message(1, tensor);
    Message v; v.string(1, name); v.message(2, t); return v;
}
void write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream out(file, std::ios::binary); out << text;
    check(static_cast<bool>(out), "cannot write fixture");
}
void fixture(const std::filesystem::path& path, bool nonfinite = false) {
    std::filesystem::create_directories(path / "tokenizer");
    write(path / "rl_agent_config.json", R"({"max_len":32,"head_max_len":24,"temperature":[2,1,1],"temperature_by_options":{"choice:2":0.1}})");
    write(path / "tokenizer/tokenizer_config.json", R"({"cls_token":"[CLS]","sep_token":"[SEP]","pad_token":"[PAD]","mask_token":"[MASK]"})");
    write(path / "tokenizer/tokenizer.json", R"({"version":"1.0","truncation":null,"padding":null,"added_tokens":[{"id":0,"content":"[PAD]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true},{"id":1,"content":"[CLS]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true},{"id":2,"content":"[SEP]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true},{"id":3,"content":"[MASK]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true}],"normalizer":null,"pre_tokenizer":{"type":"Whitespace"},"post_processor":null,"decoder":null,"model":{"type":"WordLevel","vocab":{"[PAD]":0,"[CLS]":1,"[SEP]":2,"[MASK]":3,"[UNK]":4,"choice":5,"score":6,"noul":7,"question":8,":":9,"Choose":10,"A":11,"B":12,"C":13,"level":14,"0":15,"1":16,"2":17,"false":18,"true":19,"yes":20,"no":21,"state":22,"한국어":23},"unk_token":"[UNK]"}})");
    Message graph; graph.string(2, "LayaContractFixture");
    auto cast = node("markers", "Cast", {"marker_pos"}, "marker_float");
    Message to; to.string(1, "to"); to.integer(20,2); to.integer(3,1); cast.message(5,to);
    graph.message(1, cast);
    Message scale; scale.integer(2,1); scale.string(8,"scale");
    const auto bits=std::bit_cast<std::uint32_t>(nonfinite ? std::numeric_limits<float>::quiet_NaN() : 1.F);
    std::string raw; for(unsigned i=0;i<4;++i) raw.push_back(static_cast<char>((bits>>(8*i))&255));
    scale.string(9,raw); graph.message(5,scale);
    graph.message(1,node("scores","Mul",{"marker_float","scale"},"logits"));
    // One action-logit pair per question. The graph deliberately accepts all official inputs.
    Message zero; zero.integer(1,1); zero.integer(1,2); zero.integer(2,1); zero.string(8,"actions");
    const char data[8]{}; zero.string(9,std::string_view(data,8)); graph.message(5,zero);
    auto shape = node("batch", "Shape", {"qtype"}, "batch_shape"); graph.message(1,shape);
    Message two; two.integer(1,1); two.integer(2,7); two.string(8,"two"); two.integer(7,2); graph.message(5,two);
    auto concat = node("action_shape", "Concat", {"batch_shape","two"}, "action_shape");
    Message axis; axis.string(1,"axis"); axis.integer(20,2); axis.integer(3,0); concat.message(5,axis); graph.message(1,concat);
    graph.message(1,node("actions", "Expand", {"actions","action_shape"}, "act_logits"));
    graph.message(11,value("input_ids",7,{"batch","sequence"}));
    graph.message(11,value("attention_mask",7,{"batch","sequence"}));
    graph.message(11,value("marker_pos",7,{"batch","markers"}));
    graph.message(11,value("marker_mask",9,{"batch","markers"}));
    graph.message(11,value("qtype",7,{"batch"}));
    graph.message(12,value("logits",1,{"batch","markers"}));
    graph.message(12,value("act_logits",1,{"batch","actions"}));
    Message opset; opset.integer(2,18);
    Message model; model.integer(1,8); model.string(2,"iiDecision test fixture"); model.message(7,graph); model.message(8,opset);
    write(path / "laya.onnx", model.bytes);
}
LayaQuestion choice() { return {"action","Choose",LayaQuestionType::Choice,{{"A",""},{"B",""}}}; }
void typed(const std::filesystem::path& path) {
    LayaDecisionMaker maker(path);
    const std::array qs{choice(),LayaQuestion{"severity","Choose",LayaQuestionType::Score,{{"0","no"},{"1","yes"},{"2","state"}}},
        LayaQuestion{"allowed","Choose",LayaQuestionType::Noul,{{"false","no"},{"true","yes"}}}};
    const auto r = maker.predict("state 한국어",qs);
    check(r.size()==3 && r[0].decision.candidate_id=="B", "typed question order / choice");
    // Fixture marker positions are 6 and 8. choice:2 clamps 0.1 to 0.5.
    const long double expected=1/(1+std::exp(-4.L));
    check(std::abs(r[0].probabilities[1].probability-expected)<1e-15L, "upstream temperature bucket and clamp");
    check(r[0].temperature==0.5L && r[0].probabilities[0].candidate_id=="A", "candidate identity");
    check(r[1].score>1 && r[1].score<2 && r[2].noul>0.5L, "score expectation and noul probability");
    check(r[0].answer_confidence==r[0].probabilities[1].probability && r[0].confidence<r[0].answer_confidence, "two confidence scales");
    check(r[0].act_probability==0.5L && r[0].state_tokens==2 && r[0].state_tokens_dropped==0, "action head / UTF-8 tokenization");
    check(r[0].execution.cpu_rows==3 && r[0].execution.records.size()==1 && r[0].execution.records[0].completed, "one completed forward pass");
    auto one=choice(); one.options.resize(1);
    const auto single=maker.predict("",std::array{one})[0];
    check(single.probabilities[0].probability==1 && single.confidence==1, "one option");
    check(maker.predict("",{}).empty(), "empty questions");
    const std::array candidates{Candidate{"A"},Candidate{"B"}};
    IDecisionMaker& generic=maker;
    check(generic.decide(Context{{{"signal","yes"}}},candidates).candidate_id=="B", "IDecisionMaker integration");
    const auto x=maker.predict("state [MASK] 한국어",std::array{choice()})[0];
    check(x.state_tokens==2, "literal mask must not inject option markers");
}
void budgets(const std::filesystem::path& path) {
    LayaDecisionMaker maker(path,{.max_length=16,.head_max_length=12});
    std::string state; for(int i=0;i<50;++i) state+="state ";
    const auto r=maker.predict(state,std::array{choice()})[0];
    check(r.state_tokens==50 && r.state_tokens_dropped>0 && r.input_tokens==16, "state truncation accounting");
    LayaDecisionMaker tiny(path,{.max_length=6,.head_max_length=12});
    rejects<std::invalid_argument>([&]{tiny.predict("",std::array{choice()});});
    auto q=choice(); q.options={{"A",std::string(400,'x')},{"B",std::string(400,'x')}};
    // Distinct labels remain visible; long descriptions are capped.
    check(maker.predict("",std::array{q})[0].options_distinct==2,"per-option cap");
}
void validation(const std::filesystem::path& path) {
    LayaDecisionMaker maker(path);
    auto q=choice(); q.options.clear(); rejects<std::invalid_argument>([&]{maker.predict("",std::array{q});});
    q=choice(); q.options[1].id="A"; rejects<std::invalid_argument>([&]{maker.predict("",std::array{q});});
    q=choice(); q.type=static_cast<LayaQuestionType>(7); rejects<std::invalid_argument>([&]{maker.predict("",std::array{q});});
    q=choice(); q.instructions="  "; rejects<std::invalid_argument>([&]{maker.predict("",std::array{q});});
    q=choice(); rejects<std::invalid_argument>([&]{maker.predict("",std::array{q,q});});
    q.type=LayaQuestionType::Noul; rejects<std::invalid_argument>([&]{maker.predict("",std::array{q});});
    q.type=LayaQuestionType::Score; rejects<std::invalid_argument>([&]{maker.predict("",std::array{q});});
    rejects<std::invalid_argument>([&]{LayaDecisionMaker bad(path,{.intra_op_threads=0});});
    rejects<std::runtime_error>([&]{LayaDecisionMaker bad(path/"missing");});
    rejects<std::invalid_argument>([&]{maker.decide(Context{{{"x","a"},{"x","b"}}},std::array{Candidate{"A"}});});
    rejects<std::invalid_argument>([&]{maker.evaluate("",{});});
}
void verifyConcurrentInference(const std::filesystem::path& path) {
    LayaDecisionMaker maker(path);
    std::vector<std::future<void>> workers;
    for(int i=0;i<4;++i) workers.push_back(std::async(std::launch::async,[&]{for(int j=0;j<10;++j)
        check(maker.predict("state 한국어",std::array{choice()})[0].decision.candidate_id=="B","concurrent inference");}));
    for(auto& w:workers) w.get();
}
void failures(const std::filesystem::path& path) {
    write(path/"laya.onnx","broken graph");
    rejects<std::runtime_error>([&]{LayaDecisionMaker bad(path);});
    fixture(path,true); LayaDecisionMaker nan(path);
    rejects<std::runtime_error>([&]{nan.predict("state",std::array{choice()});});
    fixture(path); LayaDecisionMaker valid(path);
    const std::string invalid_utf8(1,static_cast<char>(0xff));
    rejects<std::invalid_argument>([&]{valid.predict(invalid_utf8,std::array{choice()});});
    write(path/"rl_agent_config.json",R"({"max_len":false})");
    rejects<std::runtime_error>([&]{LayaDecisionMaker bad(path);});
    fixture(path);
    write(path/"tokenizer/tokenizer_config.json",R"({"cls_token":"missing"})");
    rejects<std::runtime_error>([&]{LayaDecisionMaker bad(path);});
    fixture(path);
    std::ifstream input(path/"laya.onnx",std::ios::binary);
    std::string graph((std::istreambuf_iterator<char>(input)),{});
    const auto pos=graph.find("marker_mask"); check(pos!=std::string::npos,"fixture input name");
    graph.replace(pos,11,"marker_mosk"); write(path/"laya.onnx",graph);
    rejects<std::runtime_error>([&]{LayaDecisionMaker bad(path);});
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"test case and scratch directory are required");
        const std::string name=argv[1];
        if(name=="Availability") {
#ifdef IIDECISION_TEST_ORT_API_VERSION
            check(OrtGetApiBase()->GetApi(IIDECISION_TEST_ORT_API_VERSION)!=nullptr,"The loaded ONNX DLL must support the compiled SDK API version");
#endif
            if(!LayaDecisionMaker::available()) rejects<std::runtime_error>([]{LayaDecisionMaker maker("missing");});
            return 0;
        }
        if(!LayaDecisionMaker::available()) return 77;
        const std::filesystem::path path=std::filesystem::path(argv[2])/name; fixture(path);
        if(name=="Typed") typed(path);
        else if(name=="Budgets") budgets(path);
        else if(name=="Validation") validation(path);
        else if(name=="Concurrency") verifyConcurrentInference(path);
        else if(name=="Failures") failures(path);
        else throw std::runtime_error("unknown test");
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
