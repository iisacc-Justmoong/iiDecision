#include "Backend.h"
namespace iiDecision::detail { void addMetal(BackendList& result) {
    result.push_back(std::make_shared<Unavailable>("metal","Metal","built without Apple Metal"));
} }
