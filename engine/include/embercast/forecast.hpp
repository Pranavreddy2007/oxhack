#pragma once
// IForecastBackend: rollout(ensemble, params, horizon, out).
// Backends: cpu_naive (Step 3), cpu_opt (Step 6), opencl (Step 12, optional).

namespace embercast {

class IForecastBackend {
 public:
  virtual ~IForecastBackend() = default;
  virtual const char* name() const = 0;
};

}  // namespace embercast
