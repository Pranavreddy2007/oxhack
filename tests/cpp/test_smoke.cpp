#include <string_view>

#include "doctest.h"
#include "embercast/clock.hpp"
#include "embercast/filter.hpp"
#include "embercast/forecast.hpp"
#include "embercast/metrics.hpp"
#include "embercast/model.hpp"
#include "embercast/policy.hpp"
#include "embercast/rng.hpp"
#include "embercast/transport.hpp"
#include "embercast/types.hpp"

TEST_CASE("clock is monotonic") {
  const auto a = embercast::now_ns();
  const auto b = embercast::now_ns();
  CHECK(b >= a);
}

TEST_CASE("safety notice text") {
  CHECK(std::string_view(embercast::safety_notice()) ==
        "Simulation only. Not for operational use.");
}
