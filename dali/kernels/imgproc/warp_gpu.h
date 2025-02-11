// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DALI_KERNELS_IMGPROC_WARP_GPU_H_
#define DALI_KERNELS_IMGPROC_WARP_GPU_H_

#include "dali/core/common.h"
#include "dali/core/geom/vec.h"
#include "dali/kernels/kernel.h"
#include "dali/kernels/imgproc/warp/warp_setup.cuh"
#include "dali/kernels/imgproc/warp/warp_variable_size_impl.cuh"
#include "dali/kernels/imgproc/warp/warp_uniform_size_impl.cuh"
#include "dali/kernels/imgproc/warp/mapping_traits.h"

namespace dali {
namespace kernels {

/// @remarks Assume HWC layout
template <typename _Mapping, int ndim, typename _OutputType, typename _InputType,
          typename _BorderType>
class WarpGPU {
 public:
  using WarpSetup = warp::WarpSetup<ndim, _OutputType, _InputType>;
  static constexpr int spatial_ndim = ndim;
  static constexpr int tensor_ndim = ndim + 1;

  using Mapping = _Mapping;
  using OutputType = _OutputType;
  using InputType = _InputType;
  using BorderType = _BorderType;
  using MappingParams = warp::mapping_params_t<Mapping>;
  static_assert(std::is_pod<MappingParams>::value, "Mapping parameters must be POD.");
  static_assert(std::is_pod<BorderType>::value, "BorderType must be POD.");

  using SampleDesc = typename WarpSetup::SampleDesc;
  using BlockDesc = typename WarpSetup::BlockDesc;
  static_assert(spatial_ndim == 2, "Not implemented for spatial_ndim != 2");

  KernelRequirements Setup(KernelContext &context,
                           const InListGPU<InputType, tensor_ndim> &in,
                           const InTensorGPU<MappingParams, 1> &mapping,
                           span<const TensorShape<spatial_ndim>> output_sizes,
                           span<const DALIInterpType> interp,
                           BorderType border = {}) {
    assert(in.size() == static_cast<size_t>(output_sizes.size()));
    auto out_shapes = setup.GetOutputShape(in.shape, output_sizes);
    return setup.Setup(out_shapes);
  }

  void Run(KernelContext &context,
           const OutListGPU<OutputType, tensor_ndim> &out,
           const InListGPU<InputType, tensor_ndim> &in,
           const InTensorGPU<MappingParams, 1> &mapping,
           span<const TensorShape<spatial_ndim>> output_sizes,
           span<const DALIInterpType> interp,
           BorderType border = {}) {
    setup.ValidateOutputShape(out.shape, in.shape, output_sizes);
    setup.PrepareSamples(out, in, interp);
    SampleDesc *gpu_samples;
    BlockDesc *gpu_blocks;

    dim3 grid_dim  = setup.GridDim();
    dim3 block_dim = setup.BlockDim();

    if (setup.IsUniformSize()) {
      gpu_samples = context.scratchpad->ToGPU(context.gpu.stream, setup.Samples());
      CUDA_CALL(cudaGetLastError());

      warp::BatchWarpUniformSize
        <Mapping, spatial_ndim, OutputType, InputType, BorderType>
        <<<grid_dim, block_dim, 0, context.gpu.stream>>>(
          gpu_samples,
          setup.UniformOutputSize(),
          setup.UniformBlockSize(),
          mapping.data,
          border);
      CUDA_CALL(cudaGetLastError());
    } else {
      std::tie(gpu_samples, gpu_blocks) = context.scratchpad->ToContiguousGPU(
        context.gpu.stream, setup.Samples(), setup.Blocks());
      CUDA_CALL(cudaGetLastError());

      warp::BatchWarpVariableSize
        <Mapping, spatial_ndim, OutputType, InputType, BorderType>
        <<<grid_dim, block_dim, 0, context.gpu.stream>>>(
          gpu_samples,
          gpu_blocks,
          mapping.data,
          border);
      CUDA_CALL(cudaGetLastError());
    }
  }

 private:
  WarpSetup setup;
  friend class WarpPrivateTest;
};

}  // namespace kernels
}  // namespace dali

#endif  // DALI_KERNELS_IMGPROC_WARP_GPU_H_
