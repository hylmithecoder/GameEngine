#pragma once

#include "../core_engine/Debugger.hpp"

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <CL/opencl.h>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define CL_CHECK_RESULT(f)                                                     \
  {                                                                            \
    cl_int res = (f);                                                          \
    if (res != CL_SUCCESS) {                                                   \
      std::cerr << "OpenCL error: " << Optimizer::errorString(res) << " ("     \
                << res << ") in " << __FILE__ << " at line " << __LINE__       \
                << std::endl;                                                  \
      assert(res == CL_SUCCESS);                                               \
    }                                                                          \
  }

namespace Optimizer {

struct OpenCLContext {
  cl_platform_id platform = nullptr;
  cl_device_id device = nullptr;
  cl_context context = nullptr;
  cl_command_queue queue = nullptr;
  cl_uint numPlatforms = 0;
  cl_uint numDevices = 0;

  cl_device_type deviceType = CL_DEVICE_TYPE_GPU;
  std::string deviceName;
  std::string deviceVendor;
  std::string deviceVersion;
  std::string clDriverVersion;
  cl_uint maxComputeUnits = 0;
  size_t maxWorkGroupSize = 0;
  cl_ulong globalMemSize = 0;
  cl_ulong localMemSize = 0;

  cl_program program = nullptr;
  cl_kernel kernel = nullptr;
};

class Optimizer {
public:
  Optimizer();
  ~Optimizer();

  bool Init(cl_device_type deviceType = CL_DEVICE_TYPE_GPU);

  const OpenCLContext &GetContext() const { return ctx; }

  cl_platform_id GetPlatform() const { return ctx.platform; }
  cl_device_id GetDevice() const { return ctx.device; }
  cl_context GetContextHandle() const { return ctx.context; }
  cl_command_queue GetQueue() const { return ctx.queue; }

  static std::string errorString(cl_int error);

  // Setup and run the physics optimization kernel
  bool SetupPhysicsKernel(const std::string &kernelSourcePath);
  bool RunPhysicsSim(float *positions, float *velocities, int numObjects,
                     float gravityY, float deltaTime);

protected:
  OpenCLContext ctx;

private:
  bool SelectPlatform();
  bool SelectDevice(cl_device_type deviceType);
  bool CreateContext();
  bool CreateQueue();
  void QueryDeviceInfo();
};

typedef Optimizer *OptimizerImplPtr;

} // namespace Optimizer
