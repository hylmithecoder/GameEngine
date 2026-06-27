#include "../../../include/optimizer/Optimizer.hpp"
#include <fstream>

namespace Optimizer {

Optimizer::Optimizer() {}

Optimizer::~Optimizer() {
  if (ctx.kernel) {
    clReleaseKernel(ctx.kernel);
    ctx.kernel = nullptr;
  }
  if (ctx.program) {
    clReleaseProgram(ctx.program);
    ctx.program = nullptr;
  }
  if (ctx.queue) {
    clReleaseCommandQueue(ctx.queue);
    ctx.queue = nullptr;
  }
  if (ctx.context) {
    clReleaseContext(ctx.context);
    ctx.context = nullptr;
  }
}

bool Optimizer::Init(cl_device_type deviceType) {
  if (!SelectPlatform())
    return false;
  if (!SelectDevice(deviceType))
    return false;
  QueryDeviceInfo();
  if (!CreateContext())
    return false;
  if (!CreateQueue())
    return false;

  Debug::Log("OpenCL initialized successfully", Debug::LogLevel::SUCCESS);
  Debug::Log("Device: " + ctx.deviceName, Debug::LogLevel::INFO);
  Debug::Log("Vendor: " + ctx.deviceVendor, Debug::LogLevel::INFO);
  Debug::Log("Version: " + ctx.deviceVersion, Debug::LogLevel::INFO);
  Debug::Log("Compute Units: " + std::to_string(ctx.maxComputeUnits),
             Debug::LogLevel::INFO);

  return true;
}

bool Optimizer::SelectPlatform() {
  cl_uint numPlatforms = 0;
  CL_CHECK_RESULT(clGetPlatformIDs(0, nullptr, &numPlatforms));

  if (numPlatforms == 0) {
    Debug::Log("No OpenCL platforms found", Debug::LogLevel::ERROR);
    return false;
  }

  std::vector<cl_platform_id> platforms(numPlatforms);
  CL_CHECK_RESULT(clGetPlatformIDs(numPlatforms, platforms.data(), nullptr));

  ctx.platform = platforms[0];
  ctx.numPlatforms = numPlatforms;

  char name[1024];
  clGetPlatformInfo(ctx.platform, CL_PLATFORM_NAME, sizeof(name), name,
                    nullptr);
  Debug::Log("Using platform: " + std::string(name), Debug::LogLevel::INFO);

  return true;
}

bool Optimizer::SelectDevice(cl_device_type deviceType) {
  cl_uint numDevices = 0;
  cl_int err =
      clGetDeviceIDs(ctx.platform, deviceType, 0, nullptr, &numDevices);

  if (err != CL_SUCCESS || numDevices == 0) {
    Debug::Log("No devices found for requested type, trying all types",
               Debug::LogLevel::WARNING);
    err = clGetDeviceIDs(ctx.platform, CL_DEVICE_TYPE_ALL, 0, nullptr,
                         &numDevices);
    if (err != CL_SUCCESS || numDevices == 0) {
      Debug::Log("No OpenCL devices found", Debug::LogLevel::ERROR);
      return false;
    }
    deviceType = CL_DEVICE_TYPE_ALL;
  }

  std::vector<cl_device_id> devices(numDevices);
  CL_CHECK_RESULT(clGetDeviceIDs(ctx.platform, deviceType, numDevices,
                                 devices.data(), nullptr));

  ctx.device = devices[0];
  ctx.numDevices = numDevices;
  ctx.deviceType = deviceType;

  return true;
}

bool Optimizer::CreateContext() {
  cl_int err;
  ctx.context =
      clCreateContext(nullptr, 1, &ctx.device, nullptr, nullptr, &err);
  CL_CHECK_RESULT(err);
  return err == CL_SUCCESS;
}

bool Optimizer::CreateQueue() {
  cl_int err;
  ctx.queue = clCreateCommandQueueWithProperties(ctx.context, ctx.device,
                                                 nullptr, &err);
  CL_CHECK_RESULT(err);
  return err == CL_SUCCESS;
}

void Optimizer::QueryDeviceInfo() {
  char buffer[1024];
  size_t size;

  clGetDeviceInfo(ctx.device, CL_DEVICE_NAME, sizeof(buffer), buffer, &size);
  ctx.deviceName = std::string(buffer, size - 1);

  clGetDeviceInfo(ctx.device, CL_DEVICE_VENDOR, sizeof(buffer), buffer, &size);
  ctx.deviceVendor = std::string(buffer, size - 1);

  clGetDeviceInfo(ctx.device, CL_DEVICE_VERSION, sizeof(buffer), buffer, &size);
  ctx.deviceVersion = std::string(buffer, size - 1);

  clGetDeviceInfo(ctx.device, CL_DRIVER_VERSION, sizeof(buffer), buffer, &size);
  ctx.clDriverVersion = std::string(buffer, size - 1);

  clGetDeviceInfo(ctx.device, CL_DEVICE_MAX_COMPUTE_UNITS,
                  sizeof(ctx.maxComputeUnits), &ctx.maxComputeUnits, nullptr);

  clGetDeviceInfo(ctx.device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                  sizeof(ctx.maxWorkGroupSize), &ctx.maxWorkGroupSize, nullptr);

  clGetDeviceInfo(ctx.device, CL_DEVICE_GLOBAL_MEM_SIZE,
                  sizeof(ctx.globalMemSize), &ctx.globalMemSize, nullptr);

  clGetDeviceInfo(ctx.device, CL_DEVICE_LOCAL_MEM_SIZE,
                  sizeof(ctx.localMemSize), &ctx.localMemSize, nullptr);
}

std::string Optimizer::errorString(cl_int error) {
  switch (error) {
  case CL_SUCCESS:
    return "CL_SUCCESS";
  case CL_DEVICE_NOT_FOUND:
    return "CL_DEVICE_NOT_FOUND";
  case CL_DEVICE_NOT_AVAILABLE:
    return "CL_DEVICE_NOT_AVAILABLE";
  case CL_COMPILER_NOT_AVAILABLE:
    return "CL_COMPILER_NOT_AVAILABLE";
  case CL_MEM_OBJECT_ALLOCATION_FAILURE:
    return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
  case CL_OUT_OF_RESOURCES:
    return "CL_OUT_OF_RESOURCES";
  case CL_OUT_OF_HOST_MEMORY:
    return "CL_OUT_OF_HOST_MEMORY";
  case CL_PROFILING_INFO_NOT_AVAILABLE:
    return "CL_PROFILING_INFO_NOT_AVAILABLE";
  case CL_MEM_COPY_OVERLAP:
    return "CL_MEM_COPY_OVERLAP";
  case CL_IMAGE_FORMAT_MISMATCH:
    return "CL_IMAGE_FORMAT_MISMATCH";
  case CL_IMAGE_FORMAT_NOT_SUPPORTED:
    return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
  case CL_BUILD_PROGRAM_FAILURE:
    return "CL_BUILD_PROGRAM_FAILURE";
  case CL_MAP_FAILURE:
    return "CL_MAP_FAILURE";
  case CL_MISALIGNED_SUB_BUFFER_OFFSET:
    return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
  case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST:
    return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
  case CL_COMPILE_PROGRAM_FAILURE:
    return "CL_COMPILE_PROGRAM_FAILURE";
  case CL_LINKER_NOT_AVAILABLE:
    return "CL_LINKER_NOT_AVAILABLE";
  case CL_LINK_PROGRAM_FAILURE:
    return "CL_LINK_PROGRAM_FAILURE";
  case CL_DEVICE_PARTITION_FAILED:
    return "CL_DEVICE_PARTITION_FAILED";
  case CL_KERNEL_ARG_INFO_NOT_AVAILABLE:
    return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";
  case CL_INVALID_VALUE:
    return "CL_INVALID_VALUE";
  case CL_INVALID_DEVICE_TYPE:
    return "CL_INVALID_DEVICE_TYPE";
  case CL_INVALID_PLATFORM:
    return "CL_INVALID_PLATFORM";
  case CL_INVALID_DEVICE:
    return "CL_INVALID_DEVICE";
  case CL_INVALID_CONTEXT:
    return "CL_INVALID_CONTEXT";
  case CL_INVALID_QUEUE_PROPERTIES:
    return "CL_INVALID_QUEUE_PROPERTIES";
  case CL_INVALID_COMMAND_QUEUE:
    return "CL_INVALID_COMMAND_QUEUE";
  case CL_INVALID_HOST_PTR:
    return "CL_INVALID_HOST_PTR";
  case CL_INVALID_MEM_OBJECT:
    return "CL_INVALID_MEM_OBJECT";
  case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:
    return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
  case CL_INVALID_IMAGE_SIZE:
    return "CL_INVALID_IMAGE_SIZE";
  case CL_INVALID_SAMPLER:
    return "CL_INVALID_SAMPLER";
  case CL_INVALID_BINARY:
    return "CL_INVALID_BINARY";
  case CL_INVALID_BUILD_OPTIONS:
    return "CL_INVALID_BUILD_OPTIONS";
  case CL_INVALID_PROGRAM:
    return "CL_INVALID_PROGRAM";
  case CL_INVALID_PROGRAM_EXECUTABLE:
    return "CL_INVALID_PROGRAM_EXECUTABLE";
  case CL_INVALID_KERNEL_NAME:
    return "CL_INVALID_KERNEL_NAME";
  case CL_INVALID_KERNEL_DEFINITION:
    return "CL_INVALID_KERNEL_DEFINITION";
  case CL_INVALID_KERNEL:
    return "CL_INVALID_KERNEL";
  case CL_INVALID_ARG_INDEX:
    return "CL_INVALID_ARG_INDEX";
  case CL_INVALID_ARG_VALUE:
    return "CL_INVALID_ARG_VALUE";
  case CL_INVALID_ARG_SIZE:
    return "CL_INVALID_ARG_SIZE";
  case CL_INVALID_KERNEL_ARGS:
    return "CL_INVALID_KERNEL_ARGS";
  case CL_INVALID_WORK_DIMENSION:
    return "CL_INVALID_WORK_DIMENSION";
  case CL_INVALID_WORK_GROUP_SIZE:
    return "CL_INVALID_WORK_GROUP_SIZE";
  case CL_INVALID_WORK_ITEM_SIZE:
    return "CL_INVALID_WORK_ITEM_SIZE";
  case CL_INVALID_GLOBAL_OFFSET:
    return "CL_INVALID_GLOBAL_OFFSET";
  case CL_INVALID_EVENT_WAIT_LIST:
    return "CL_INVALID_EVENT_WAIT_LIST";
  case CL_INVALID_EVENT:
    return "CL_INVALID_EVENT";
  case CL_INVALID_OPERATION:
    return "CL_INVALID_OPERATION";
  case CL_INVALID_GL_OBJECT:
    return "CL_INVALID_GL_OBJECT";
  case CL_INVALID_BUFFER_SIZE:
    return "CL_INVALID_BUFFER_SIZE";
  case CL_INVALID_MIP_LEVEL:
    return "CL_INVALID_MIP_LEVEL";
  case CL_INVALID_GLOBAL_WORK_SIZE:
    return "CL_INVALID_GLOBAL_WORK_SIZE";
  case CL_INVALID_PROPERTY:
    return "CL_INVALID_PROPERTY";
  default:
    return "Unknown OpenCL error";
  }
}

bool Optimizer::SetupPhysicsKernel(const std::string &kernelSourcePath) {
  if (ctx.context == nullptr) {
    Debug::Log("OpenCL Context not initialized. Call Init first.",
               Debug::LogLevel::ERROR);
    return false;
  }

  // Load kernel file
  std::ifstream file(kernelSourcePath);
  if (!file.is_open()) {
    Debug::Log("Failed to open OpenCL kernel file: " + kernelSourcePath,
               Debug::LogLevel::ERROR);
    return false;
  }

  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  file.close();

  const char *sourceStr = source.c_str();
  size_t sourceSize = source.length();

  cl_int err;
  ctx.program =
      clCreateProgramWithSource(ctx.context, 1, &sourceStr, &sourceSize, &err);
  CL_CHECK_RESULT(err);
  if (err != CL_SUCCESS)
    return false;

  err = clBuildProgram(ctx.program, 1, &ctx.device, nullptr, nullptr, nullptr);
  if (err != CL_SUCCESS) {
    char buildLog[4096];
    clGetProgramBuildInfo(ctx.program, ctx.device, CL_PROGRAM_BUILD_LOG,
                          sizeof(buildLog), buildLog, nullptr);
    Debug::Log("OpenCL Program Build Failure log: " + std::string(buildLog),
               Debug::LogLevel::ERROR);
    return false;
  }

  ctx.kernel = clCreateKernel(ctx.program, "apply_gravity", &err);
  CL_CHECK_RESULT(err);
  if (err != CL_SUCCESS)
    return false;

  Debug::Log("OpenCL physics kernel compiled and created successfully.",
             Debug::LogLevel::SUCCESS);
  return true;
}

bool Optimizer::RunPhysicsSim(float *positions, float *velocities,
                              int numObjects, float gravityY, float deltaTime) {
  if (ctx.kernel == nullptr || ctx.queue == nullptr) {
    Debug::Log("Physics kernel not setup. Call SetupPhysicsKernel first.",
               Debug::LogLevel::ERROR);
    return false;
  }

  if (numObjects <= 0)
    return true;

  cl_int err;
  size_t dataSize = sizeof(float) * 4 * numObjects; // float4 per object

  // 1. Create buffers on GPU
  cl_mem bufPos =
      clCreateBuffer(ctx.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                     dataSize, positions, &err);
  CL_CHECK_RESULT(err);
  if (err != CL_SUCCESS)
    return false;

  cl_mem bufVel =
      clCreateBuffer(ctx.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                     dataSize, velocities, &err);
  CL_CHECK_RESULT(err);
  if (err != CL_SUCCESS) {
    clReleaseMemObject(bufPos);
    return false;
  }

  // 2. Set arguments
  float gravity[4] = {0.0f, gravityY, 0.0f, 0.0f};

  err = clSetKernelArg(ctx.kernel, 0, sizeof(cl_mem), &bufPos);
  CL_CHECK_RESULT(err);
  err = clSetKernelArg(ctx.kernel, 1, sizeof(cl_mem), &bufVel);
  CL_CHECK_RESULT(err);
  err = clSetKernelArg(ctx.kernel, 2, sizeof(float) * 4, gravity);
  CL_CHECK_RESULT(err);
  err = clSetKernelArg(ctx.kernel, 3, sizeof(float), &deltaTime);
  CL_CHECK_RESULT(err);
  err = clSetKernelArg(ctx.kernel, 4, sizeof(int), &numObjects);
  CL_CHECK_RESULT(err);

  // 3. Enqueue kernel
  size_t globalWorkSize = numObjects;
  err = clEnqueueNDRangeKernel(ctx.queue, ctx.kernel, 1, nullptr,
                               &globalWorkSize, nullptr, 0, nullptr, nullptr);
  CL_CHECK_RESULT(err);
  if (err != CL_SUCCESS) {
    clReleaseMemObject(bufPos);
    clReleaseMemObject(bufVel);
    return false;
  }

  // 4. Read back
  err = clEnqueueReadBuffer(ctx.queue, bufPos, CL_TRUE, 0, dataSize, positions,
                            0, nullptr, nullptr);
  CL_CHECK_RESULT(err);
  err = clEnqueueReadBuffer(ctx.queue, bufVel, CL_TRUE, 0, dataSize, velocities,
                            0, nullptr, nullptr);
  CL_CHECK_RESULT(err);

  // Cleanup buffers
  clReleaseMemObject(bufPos);
  clReleaseMemObject(bufVel);

  return err == CL_SUCCESS;
}

} // namespace Optimizer
