#include <abcurves/native.hpp>
#include <cstdio>
int main() {
 std::printf("{\"planner_bytes\":%zu,\"observation_handler_bytes\":%zu,\"controller_bytes\":%zu",
             sizeof(abc::ContinuousPlanner),sizeof(abc::ObservationInput),sizeof(abc::SegmentController));
#ifdef ABC_HARDENING_SIZES
 std::printf(",\"failure_record_bytes\":%zu",sizeof(abc::PlannerFailure));
#endif
 std::printf("}\n");
 return 0;
}
