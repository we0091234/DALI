// Copyright (c) 2017-2018, NVIDIA CORPORATION. All rights reserved.
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

#include "dali/pipeline/operators/util/external_source.h"

namespace dali {

template<>
void ExternalSource<CPUBackend>::RunImpl(SampleWorkspace *ws) {
  // Wrap the output tensor around our data
  auto &output = ws->Output<CPUBackend>(0);
  cudaStream_t stream = ws->has_stream() ? ws->stream() : 0;
  if (data_in_tl_) {
    DALI_ENFORCE(OperatorBase::batch_size_ == static_cast<int>(tl_data_.ntensor()),
      "Data list provided to ExternalSource needs to have batch_size length.");
    output.Copy(tl_data_, ws->data_idx(), stream);
  } else {
    DALI_ENFORCE(OperatorBase::batch_size_ == static_cast<int>(t_data_.size()),
      "Data list provided to ExternalSource needs to have batch_size length.");
    auto &data = t_data_[ws->data_idx()];
    output.Copy(data, stream);
  }

  std::lock_guard<std::mutex> l(samples_processed_m_);
  if (++samples_processed_ >= batch_size_) {
    samples_processed_ = 0;
    {
      std::lock_guard<std::mutex> busy_lock(busy_m_);
      busy_ = false;
    }
    cv_.notify_one();
  }
}

DALI_REGISTER_OPERATOR(ExternalSource, ExternalSource<CPUBackend>, CPU);

DALI_SCHEMA(ExternalSource)
  .DocStr(R"code(Allows externally provided data to be passed as an input to the pipeline,
see :meth:`nvidia.dali.pipeline.Pipeline.feed_input` and
:meth:`nvidia.dali.pipeline.Pipeline.iter_setup`. Currently this operator is not
supported in TensorFlow. It is worth noting that fed inputs should match the number of dimensions
expected by the next operator in the pipeline (e.g. NHWC will expect 3-dimensional tensors
where the last dimension represents the different channels).)code")
  .NumInput(0)
  .NumOutput(1);

}  // namespace dali
