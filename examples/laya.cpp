#include <iiDecision/LayaDecisionMaker.h>

#include <array>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv) {
    try {
        if(argc!=2) { std::cerr<<"Usage: iiDecisionLayaExample <local-laya-package>\n"; return 2; }
        iiDecision::LayaDecisionMaker maker(argv[1]);
        const std::array questions{
            iiDecision::LayaQuestion{"route","Choose the next action for this support ticket.",iiDecision::LayaQuestionType::Choice,
                {{"refund","Issue a refund"},{"troubleshoot","Help fix the technical problem"},{"escalate","Send to a human specialist"}}},
            iiDecision::LayaQuestion{"severity","Rate the severity of the issue.",iiDecision::LayaQuestionType::Score,
                {{"0","Low"},{"1","Medium"},{"2","High"}}},
            iiDecision::LayaQuestion{"urgent","Does the customer require immediate help?",iiDecision::LayaQuestionType::Noul,
                {{"false","The request can wait"},{"true","Immediate help is needed"}}}
        };
        std::cout<<std::setprecision(12);
        for(const auto& answer:maker.predict("The application crashes on startup and the customer cannot access a paid project.",questions)) {
            std::cout<<answer.question_id<<" selected="<<answer.decision.candidate_id
                <<" confidence="<<answer.answer_confidence<<" score="<<answer.score<<" noul="<<answer.noul
                <<" state_tokens="<<answer.state_tokens<<" dropped="<<answer.state_tokens_dropped<<'\n';
            for(const auto& p:answer.probabilities) std::cout<<"  "<<p.candidate_id<<"="<<p.probability<<'\n';
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
