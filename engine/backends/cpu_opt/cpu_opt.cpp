// cpu_opt: placeholder until Step 6. It uses the reference backend.
#include "embercast/forecast.hpp"

namespace embercast {
std::unique_ptr<IForecastBackend> make_cpu_naive();
std::unique_ptr<IForecastBackend> make_cpu_opt(int) { return make_cpu_naive(); }
}  // namespace embercast
