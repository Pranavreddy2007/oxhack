#pragma once
// IPolicy: decide(forecast, state) -> Plan.
// Policies: EnsembleRisk, FrontNearest (B0), Deterministic (B1), Oracle.

namespace embercast {

class IPolicy {
 public:
  virtual ~IPolicy() = default;
  virtual const char* name() const = 0;
};

}  // namespace embercast
