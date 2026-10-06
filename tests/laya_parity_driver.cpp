#include <iiDecision/LayaDecisionMaker.h>
#include <array>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv) {
    try {
        if(argc!=2 && argc!=3) return 2;
        iiDecision::LayaDecisionMaker maker(argv[1]);
        if(argc==3 && std::string_view(argv[2])=="single") {
            const std::array single{iiDecision::LayaQuestion{"single","Choose the next action.",iiDecision::LayaQuestionType::Choice,{{"continue","Continue the workflow"}}}};
            const auto result=maker.predict("There is only one available action.",single)[0];
            if(result.decision.candidate_id!="continue" || result.probabilities[0].probability!=1) return 3;
            std::cout<<"single-option selected="<<result.decision.candidate_id<<" probability="<<result.probabilities[0].probability<<'\n';
            return 0;
        }
        const std::array questions{
            iiDecision::LayaQuestion{"route","Choose the next action for this support ticket.",iiDecision::LayaQuestionType::Choice,
                {{"refund","Issue a refund"},{"troubleshoot","Help fix the technical problem"},{"escalate","Send to a human specialist"}}},
            iiDecision::LayaQuestion{"severity","Rate the severity of the issue.",iiDecision::LayaQuestionType::Score,
                {{"0","Low"},{"1","Medium"},{"2","High"}}},
            iiDecision::LayaQuestion{"urgent","Does the customer require immediate help?",iiDecision::LayaQuestionType::Noul,
                {{"false","The request can wait"},{"true","Immediate help is needed"}}}
        };
        std::cout<<std::setprecision(18);
        std::string state;
        while(std::getline(std::cin,state)) {
            for(const auto& a:maker.predict(state,questions)) {
                std::cout<<a.question_id<<'\t'<<a.decision.candidate_id<<'\t'<<a.confidence<<'\t'<<a.answer_confidence
                    <<'\t'<<a.score<<'\t'<<a.noul<<'\t'<<a.act_probability<<'\t'<<a.input_tokens<<'\t'<<a.state_tokens
                    <<'\t'<<a.state_tokens_dropped<<'\t'<<a.options_distinct;
                for(const auto& p:a.probabilities) std::cout<<'\t'<<p.probability;
                std::cout<<'\n';
            }
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
