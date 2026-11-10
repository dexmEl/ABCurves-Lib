#pragma once
#include "abcurves/types.hpp"
#include <cstdio>
#include <new>
#include <stdexcept>
namespace abc::detail {
class GenerationError final : public std::runtime_error {
public:
    GenerationError(FailureCategory category, FailureReason reason, const char* message)
        : std::runtime_error(message), category(category), reason(reason) {}
    FailureCategory category;
    FailureReason reason;
};
// Call only from a catch handler. Fixed storage; preserves the first cause.
inline PlannerFailure current_failure() noexcept {
    PlannerFailure f; f.reset_required=true;
    const char* message="unknown native generation exception";
    try { throw; }
    catch(const GenerationError& e) {
        f.category=e.category; f.reason=e.reason;
        std::snprintf(f.message,sizeof f.message,"%s",e.what()); return f;
    }
    catch(const std::bad_alloc&) {
        f.category=FailureCategory::resource; f.reason=FailureReason::allocation;
        message="allocation failed during generation";
    }
    catch(const std::exception& e) {
        f.category=FailureCategory::internal; f.reason=FailureReason::native_exception;
        std::snprintf(f.message,sizeof f.message,"%s",e.what()); return f;
    }
    catch(...) { f.category=FailureCategory::internal; f.reason=FailureReason::unknown; }
    std::snprintf(f.message,sizeof f.message,"%s",message); return f;
}
// Preconditions are checked before generation. A logic exception escaping the
// mutation phase is an internal failure, not a recoverable input status.
[[noreturn]] inline void rethrow_generation_failure() {
    try { throw; }
    catch(const std::logic_error& e) {
        throw GenerationError(FailureCategory::internal,FailureReason::native_exception,e.what());
    }
    catch(...) { throw; }
}
} // namespace abc::detail
