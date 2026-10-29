if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiDecision")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()
foreach(_file Decision/IDecisionMaker.h Decision/FirstCandidateDecisionMaker.h
        Decision/FirstCandidateDecisionMaker.cpp Decision/ExactDecisionMaker.h
        Decision/ExactDecisionMaker.cpp Evaluation/Evaluation.h Evaluation/Evaluation.cpp
        RAG/RagDecisionGate.h RAG/RagDecisionGate.cpp Laya/LayaDecisionMaker.h
        Laya/LayaDecisionMaker.cpp Laya/LayaEvidenceScorer.h Laya/LayaEvidenceScorer.cpp
        Acceleration/Acceleration.h Acceleration/Acceleration.cpp Core/Numerics.h
        backends/Backend.h)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
