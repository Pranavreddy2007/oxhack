// opencl: one work-item per (member, cell), one kernel launch per tick.
//
// The kernel is a copy of step_reference(): the same Philox4x32-10 draws,
// the same uint32 threshold tables, the same neighbour order. All decisions
// are integer compares, so the final grids are bit-identical to cpu_naive.
// The reduction runs on the CPU with reduce_member() in member order, so
// the float sums have the cpu_naive order too (tested, AC2).
//
// The OpenCL library is loaded with dlopen() at run time. The build needs
// no OpenCL SDK or headers, and a binary without a device still runs (the
// constructor throws, and make_backend() reports the error).
// Device buffers are allocated in prepare(). The terrain is uploaded once.

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "embercast/forecast.hpp"

namespace embercast {

namespace {

// Minimal OpenCL 1.2 C API declarations (the ABI is stable).
using cl_int = std::int32_t;
using cl_uint = std::uint32_t;
using cl_ulong = std::uint64_t;
using cl_platform_id = struct _cl_platform_id*;
using cl_device_id = struct _cl_device_id*;
using cl_context = struct _cl_context*;
using cl_command_queue = struct _cl_command_queue*;
using cl_mem = struct _cl_mem*;
using cl_program = struct _cl_program*;
using cl_kernel = struct _cl_kernel*;
using cl_event = struct _cl_event*;

constexpr cl_ulong kDeviceTypeAll = 0xFFFFFFFFull;
constexpr cl_ulong kMemReadWrite = 1, kMemReadOnly = 4;
constexpr cl_uint kDeviceName = 0x102B, kProgramBuildLog = 0x1183;

struct ClApi {
  void* lib = nullptr;
  cl_int (*GetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
  cl_int (*GetDeviceIDs)(cl_platform_id, cl_ulong, cl_uint, cl_device_id*, cl_uint*);
  cl_int (*GetDeviceInfo)(cl_device_id, cl_uint, std::size_t, void*, std::size_t*);
  cl_context (*CreateContext)(const std::intptr_t*, cl_uint, const cl_device_id*, void*, void*,
                              cl_int*);
  cl_command_queue (*CreateCommandQueue)(cl_context, cl_device_id, cl_ulong, cl_int*);
  cl_mem (*CreateBuffer)(cl_context, cl_ulong, std::size_t, void*, cl_int*);
  cl_program (*CreateProgramWithSource)(cl_context, cl_uint, const char**, const std::size_t*,
                                        cl_int*);
  cl_int (*BuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
  cl_int (*GetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, std::size_t, void*,
                                std::size_t*);
  cl_kernel (*CreateKernel)(cl_program, const char*, cl_int*);
  cl_int (*SetKernelArg)(cl_kernel, cl_uint, std::size_t, const void*);
  cl_int (*EnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_uint, std::size_t, std::size_t,
                               const void*, cl_uint, const cl_event*, cl_event*);
  cl_int (*EnqueueReadBuffer)(cl_command_queue, cl_mem, cl_uint, std::size_t, std::size_t, void*,
                              cl_uint, const cl_event*, cl_event*);
  cl_int (*EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const std::size_t*,
                                 const std::size_t*, const std::size_t*, cl_uint, const cl_event*,
                                 cl_event*);
  cl_int (*Finish)(cl_command_queue);
  cl_int (*ReleaseMemObject)(cl_mem);
  cl_int (*ReleaseKernel)(cl_kernel);
  cl_int (*ReleaseProgram)(cl_program);
  cl_int (*ReleaseCommandQueue)(cl_command_queue);
  cl_int (*ReleaseContext)(cl_context);

  void load() {
    for (const char* name : {"libOpenCL.so.1", "libOpenCL.so",
                             "/System/Library/Frameworks/OpenCL.framework/OpenCL"}) {
      lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
      if (lib) break;
    }
    if (!lib) throw std::runtime_error("opencl: libOpenCL not found");
    sym(GetPlatformIDs, "clGetPlatformIDs");
    sym(GetDeviceIDs, "clGetDeviceIDs");
    sym(GetDeviceInfo, "clGetDeviceInfo");
    sym(CreateContext, "clCreateContext");
    sym(CreateCommandQueue, "clCreateCommandQueue");
    sym(CreateBuffer, "clCreateBuffer");
    sym(CreateProgramWithSource, "clCreateProgramWithSource");
    sym(BuildProgram, "clBuildProgram");
    sym(GetProgramBuildInfo, "clGetProgramBuildInfo");
    sym(CreateKernel, "clCreateKernel");
    sym(SetKernelArg, "clSetKernelArg");
    sym(EnqueueWriteBuffer, "clEnqueueWriteBuffer");
    sym(EnqueueReadBuffer, "clEnqueueReadBuffer");
    sym(EnqueueNDRangeKernel, "clEnqueueNDRangeKernel");
    sym(Finish, "clFinish");
    sym(ReleaseMemObject, "clReleaseMemObject");
    sym(ReleaseKernel, "clReleaseKernel");
    sym(ReleaseProgram, "clReleaseProgram");
    sym(ReleaseCommandQueue, "clReleaseCommandQueue");
    sym(ReleaseContext, "clReleaseContext");
  }
  template <class F>
  void sym(F& f, const char* name) {
    void* p = dlsym(lib, name);
    if (!p) throw std::runtime_error(std::string("opencl: missing symbol ") + name);
    f = reinterpret_cast<F>(p);
  }
};

void check(cl_int err, const char* what) {
  if (err != 0) throw std::runtime_error(std::string("opencl: ") + what + " failed, error " +
                                         std::to_string(err));
}

constexpr int kChunkMembers = 4;
constexpr int kTableWords = kFuelClasses * kDirs * kSlopeBins * (kMaxCrews + 1);
static_assert(sizeof(ThresholdTable) == kTableWords * sizeof(std::uint32_t));

// OpenCL C copy of philox4x32_10(), spread_draws() and step_reference().
const char* kSource = R"CLC(
__constant int DX[8] = {1, 0, -1, 0, 1, -1, -1, 1};
__constant int DY[8] = {0, 1, 0, -1, 1, 1, -1, -1};

uint4 philox(uint4 c, uint k0, uint k1) {
  for (int r = 0; r < 10; ++r) {
    const uint hi0 = mul_hi(0xD2511F53u, c.x), lo0 = 0xD2511F53u * c.x;
    const uint hi1 = mul_hi(0xCD9E8D57u, c.z), lo1 = 0xCD9E8D57u * c.z;
    c = (uint4)(hi1 ^ c.y ^ k0, lo1, hi0 ^ c.w ^ k1, lo0);
    k0 += 0x9E3779B9u;
    k1 += 0xBB67AE85u;
  }
  return c;
}

uint lane_word(uint4 v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : (i == 2 ? v.z : v.w)); }

__kernel void step(__global const uchar* in, __global uchar* out,
                   __global const uchar* fuel, __global const uchar* cluster,
                   __global const uchar* sbin, __global const uint* tables,
                   __global const uchar* crews, int n, int cells, uint seed_lo, uint seed_hi,
                   uint tick, uint stream_base, uchar B) {
  const int gid = get_global_id(0);
  const int k = gid / cells, c = gid - k * cells;
  __global const uchar* g = in + (size_t)k * cells;
  const uchar v = g[c];
  if (v == 255) { out[gid] = 255; return; }
  if (v != 0) { out[gid] = v == 1 ? 255 : (uchar)(v - 1); return; }
  out[gid] = 0;
  const int f = fuel[c];
  if (f == 0) return;
  const int x = c % n, y = c / n;
  const int ncrew = crews[cluster[c]];
  __global const uint* tab = tables + (size_t)k * KTABLE;
  bool have0 = false, have1 = false;
  uint4 d0 = (uint4)(0), d1 = (uint4)(0);
  for (int d = 0; d < 8; ++d) {
    const int nx = x + DX[d], ny = y + DY[d];
    if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
    const uchar nv = g[ny * n + nx];
    if (nv == 0 || nv == 255) continue;
    uint u;
    if (d < 4) {
      if (!have0) { d0 = philox((uint4)(c, tick, k, stream_base), seed_lo, seed_hi); have0 = true; }
      u = lane_word(d0, d);
    } else {
      if (!have1) { d1 = philox((uint4)(c, tick, k, stream_base + 1), seed_lo, seed_hi); have1 = true; }
      u = lane_word(d1, d - 4);
    }
    const uint thr = tab[((f * 8 + d) * NSB + sbin[c * 8 + d]) * NCR + ncrew];
    if (u < thr) { out[gid] = B; return; }
  }
}
)CLC";

class OpenCl final : public IForecastBackend {
 public:
  OpenCl() {
    cl_.load();
    cl_uint np = 0;
    cl_platform_id plats[8];
    check(cl_.GetPlatformIDs(8, plats, &np), "clGetPlatformIDs");
    for (cl_uint i = 0; i < np && !dev_; ++i) {
      cl_uint nd = 0;
      if (cl_.GetDeviceIDs(plats[i], kDeviceTypeAll, 1, &dev_, &nd) != 0 || nd == 0) dev_ = nullptr;
    }
    if (!dev_) throw std::runtime_error("opencl: no OpenCL device");
    char name[256] = {};
    cl_.GetDeviceInfo(dev_, kDeviceName, sizeof(name) - 1, name, nullptr);
    device_name_ = name;
    cl_int err = 0;
    ctx_ = cl_.CreateContext(nullptr, 1, &dev_, nullptr, nullptr, &err);
    check(err, "clCreateContext");
    q_ = cl_.CreateCommandQueue(ctx_, dev_, 0, &err);
    check(err, "clCreateCommandQueue");
    const char* src = kSource;
    prog_ = cl_.CreateProgramWithSource(ctx_, 1, &src, nullptr, &err);
    check(err, "clCreateProgramWithSource");
    const std::string opts = "-DKTABLE=" + std::to_string(kTableWords) +
                             " -DNSB=" + std::to_string(kSlopeBins) +
                             " -DNCR=" + std::to_string(kMaxCrews + 1);
    if (cl_.BuildProgram(prog_, 1, &dev_, opts.c_str(), nullptr, nullptr) != 0) {
      char log[4096] = {};
      cl_.GetProgramBuildInfo(prog_, dev_, kProgramBuildLog, sizeof(log) - 1, log, nullptr);
      throw std::runtime_error(std::string("opencl: kernel build failed: ") + log);
    }
    kern_ = cl_.CreateKernel(prog_, "step", &err);
    check(err, "clCreateKernel");
  }

  ~OpenCl() override {
    release_buffers();
    if (kern_) cl_.ReleaseKernel(kern_);
    if (prog_) cl_.ReleaseProgram(prog_);
    if (q_) cl_.ReleaseCommandQueue(q_);
    if (ctx_) cl_.ReleaseContext(ctx_);
  }

  const char* name() const override { return "opencl"; }
  const std::string& device_name() const { return device_name_; }

  void prepare(int K, int cells) override {
    release_buffers();
    K_ = K;
    cells_ = cells;
    const std::size_t grid = std::size_t(K) * cells;
    a_ = buffer(kMemReadWrite, grid);
    b_ = buffer(kMemReadWrite, grid);
    fuel_ = buffer(kMemReadOnly, cells);
    cluster_ = buffer(kMemReadOnly, cells);
    sbin_ = buffer(kMemReadOnly, std::size_t(cells) * kDirs);
    tables_ = buffer(kMemReadOnly, std::size_t(K) * sizeof(ThresholdTable));
    crews_ = buffer(kMemReadOnly, sizeof(CrewCounts));
    ends_.assign(grid, 0);
    terrain_ = nullptr;
  }

  void advance(Ensemble& ens, const StepArgs& s) override {
    upload(ens, s);
    write(a_, ens.state.data(), ens.state.size());
    launch(a_, b_, s, s.tick, kStreamSpread);
    read(b_, ens.state.data(), ens.state.size());
  }

  void rollout(const Ensemble& ens, const StepArgs& s, int H, ForecastOut& out) override {
    out.K = ens.K;
    out.H = H;
    out.t0 = s.tick - 1;
    out.J = s.terrain->num_clusters;
    upload(ens, s);
    write(a_, ens.state.data(), ens.state.size());
    cl_mem in = a_, o = b_;
    for (int h = 0; h < H; ++h) {
      launch(in, o, s, s.tick + std::uint32_t(h), kStreamForecast);
      std::swap(in, o);
    }
    const std::size_t grid = std::size_t(ens.K) * cells_;
    read(in, ends_.data(), grid);
    std::fill(out.p_burn.begin(), out.p_burn.end(), 0.f);
    for (int k = 0; k < ens.K; ++k)
      reduce_member(*s.terrain, ens.member(k), ends_.data() + std::size_t(k) * cells_, k,
                    ens.weight[k], out);
    finish_reduce(*s.terrain, ens.weight, out);
  }

 private:
  cl_mem buffer(cl_ulong flags, std::size_t bytes) {
    cl_int err = 0;
    cl_mem m = cl_.CreateBuffer(ctx_, flags, bytes, nullptr, &err);
    check(err, "clCreateBuffer");
    return m;
  }
  void write(cl_mem m, const void* p, std::size_t bytes) {
    check(cl_.EnqueueWriteBuffer(q_, m, 0, 0, bytes, p, 0, nullptr, nullptr),
          "clEnqueueWriteBuffer");
  }
  void read(cl_mem m, void* p, std::size_t bytes) {
    check(cl_.EnqueueReadBuffer(q_, m, 1, 0, bytes, p, 0, nullptr, nullptr),
          "clEnqueueReadBuffer");
  }

  // Terrain once (on first use), tables and crews every call.
  void upload(const Ensemble& ens, const StepArgs& s) {
    if (ens.K != K_ || ens.cells != cells_) throw std::runtime_error("opencl: prepare() size");
    const Terrain& t = *s.terrain;
    if (terrain_ != &t) {
      write(fuel_, t.fuel.data(), std::size_t(cells_));
      write(cluster_, t.cluster.data(), std::size_t(cells_));
      write(sbin_, t.slope_bin.data(), std::size_t(cells_) * kDirs);
      terrain_ = &t;
      n_ = t.n;
    }
    write(tables_, ens.tables.data(), std::size_t(ens.K) * sizeof(ThresholdTable));
    write(crews_, s.crews.data(), sizeof(CrewCounts));
    burn_ticks_ = std::uint8_t(s.mc->burn_ticks);
  }

  void launch(cl_mem in, cl_mem out, const StepArgs& s, std::uint32_t tick, std::uint32_t stream) {
    const cl_uint seed_lo = cl_uint(s.seed), seed_hi = cl_uint(s.seed >> 32);
    cl_uint i = 0;
    auto arg = [&](std::size_t size, const void* p) {
      check(cl_.SetKernelArg(kern_, i++, size, p), "clSetKernelArg");
    };
    arg(sizeof(cl_mem), &in);
    arg(sizeof(cl_mem), &out);
    arg(sizeof(cl_mem), &fuel_);
    arg(sizeof(cl_mem), &cluster_);
    arg(sizeof(cl_mem), &sbin_);
    arg(sizeof(cl_mem), &tables_);
    arg(sizeof(cl_mem), &crews_);
    arg(sizeof(int), &n_);
    arg(sizeof(int), &cells_);
    arg(sizeof(cl_uint), &seed_lo);
    arg(sizeof(cl_uint), &seed_hi);
    arg(sizeof(cl_uint), &tick);
    arg(sizeof(cl_uint), &stream);
    arg(sizeof(std::uint8_t), &burn_ticks_);
    // Launch in chunks of members: one launch over all K * cells work-items
    // fails with CL_OUT_OF_RESOURCES on the UNO Q (Adreno 702, rusticl) at
    // N = 96, K = 64. get_global_id() includes the offset.
    for (int k0 = 0; k0 < K_; k0 += kChunkMembers) {
      const std::size_t offset = std::size_t(k0) * cells_;
      const std::size_t global = std::size_t(std::min(kChunkMembers, K_ - k0)) * cells_;
      check(cl_.EnqueueNDRangeKernel(q_, kern_, 1, &offset, &global, nullptr, 0, nullptr, nullptr),
            "clEnqueueNDRangeKernel");
    }
  }

  void release_buffers() {
    for (cl_mem* m : {&a_, &b_, &fuel_, &cluster_, &sbin_, &tables_, &crews_})
      if (*m) {
        cl_.ReleaseMemObject(*m);
        *m = nullptr;
      }
  }

  ClApi cl_;
  cl_device_id dev_ = nullptr;
  cl_context ctx_ = nullptr;
  cl_command_queue q_ = nullptr;
  cl_program prog_ = nullptr;
  cl_kernel kern_ = nullptr;
  cl_mem a_ = nullptr, b_ = nullptr, fuel_ = nullptr, cluster_ = nullptr, sbin_ = nullptr,
         tables_ = nullptr, crews_ = nullptr;
  std::string device_name_;
  std::vector<std::uint8_t> ends_;
  const Terrain* terrain_ = nullptr;
  int K_ = 0, cells_ = 0, n_ = 0;
  std::uint8_t burn_ticks_ = 6;
};

}  // namespace

std::unique_ptr<IForecastBackend> make_opencl() { return std::make_unique<OpenCl>(); }

}  // namespace embercast
