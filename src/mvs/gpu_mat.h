// COLMAP - Structure-from-Motion and Multi-View Stereo.
// Copyright (C) 2016  Johannes L. Schoenberger <jsch at inf.ethz.ch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef COLMAP_SRC_MVS_GPU_MAT_H_
#define COLMAP_SRC_MVS_GPU_MAT_H_

#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include <cuda_runtime.h>
#include <curand_kernel.h>

#include "mvs/cuda_flip.h"
#include "mvs/cuda_rotate.h"
#include "mvs/cuda_transpose.h"
#include "mvs/mat.h"
#include "util/cuda.h"
#include "util/cudacc.h"

namespace colmap {
namespace mvs {

template <typename T>
class GpuMat {
 public:
  GpuMat(const size_t width, const size_t height, const size_t depth = 1);
  ~GpuMat();

  __host__ __device__ const T* GetPtr() const;
  __host__ __device__ T* GetPtr();

  __host__ __device__ size_t GetPitch() const;
  __host__ __device__ size_t GetWidth() const;
  __host__ __device__ size_t GetHeight() const;
  __host__ __device__ size_t GetDepth() const;

  __device__ T Get(const size_t row, const size_t col) const;
  __device__ T Get(const size_t row, const size_t col,
                   const size_t slice) const;
  __device__ void GetSlice(const size_t row, const size_t col, T* values) const;

  __device__ T& GetRef(const size_t row, const size_t col);
  __device__ T& GetRef(const size_t row, const size_t col, const size_t slice);

  __device__ void Set(const size_t row, const size_t col, const T value);
  __device__ void Set(const size_t row, const size_t col, const size_t slice,
                      const T value);
  __device__ void SetSlice(const size_t row, const size_t col, const T* values);

  void FillWithScalar(const T value);
  void FillWithVector(const T* values);
  void FillWithRandomNumbers(const T min_value, const T max_value,
                             GpuMat<curandState> random_state);

  void CopyToDevice(const T* data, const size_t pitch);
  void CopyToHost(T* data, const size_t pitch) const;
  Mat<T> CopyToMat() const;

  // Transpose array by swapping x and y coordinates.
  void Transpose(GpuMat<T>* output);

  // Flip array along vertical axis.
  void FlipHorizontal(GpuMat<T>* output);

  // Rotate array in counter-clockwise direction.
  void Rotate(GpuMat<T>* output);

  void Read(const std::string& file_name);
  void Write(const std::string& file_name);
  void Write(const std::string& file_name, const size_t slice);

 protected:
  void ComputeCudaConfig();

  const static size_t kBlockDimX = 32;
  const static size_t kBlockDimY = 16;

  std::shared_ptr<T> array_;
  T* array_ptr_;

  size_t pitch_;
  size_t width_;
  size_t height_;
  size_t depth_;

  dim3 blockSize_;
  dim3 gridSize_;
};

////////////////////////////////////////////////////////////////////////////////
// Implementation
////////////////////////////////////////////////////////////////////////////////

#ifdef __CUDACC__

namespace internal {

template <typename T>
__global__ void FillWithVectorKernel(const T* values, GpuMat<T> output) {
  const size_t row = blockIdx.y * blockDim.y + threadIdx.y;
  const size_t col = blockIdx.x * blockDim.x + threadIdx.x;
  if (row < output.GetHeight() && col < output.GetWidth()) {
    for (size_t slice = 0; slice < output.GetDepth(); ++slice) {
      output.Set(row, col, slice, values[slice]);
    }
  }
}

template <typename T>
__global__ void FillWithRandomNumbersKernel(GpuMat<T> output,
                                            GpuMat<curandState> random_state,
                                            const T min_value,
                                            const T max_value) {
  const size_t row = blockIdx.y * blockDim.y + threadIdx.y;
  const size_t col = blockIdx.x * blockDim.x + threadIdx.x;
  if (row < output.GetHeight() && col < output.GetWidth()) {
    curandState local_state = random_state.Get(row, col);
    for (size_t slice = 0; slice < output.GetDepth(); ++slice) {
      const T random_value =
          curand_uniform(&local_state) * (max_value - min_value) + min_value;
      output.Set(row, col, slice, random_value);
    }
    random_state.Set(row, col, local_state);
  }
}

}  // namespace internal

template <typename T>
GpuMat<T>::GpuMat(const size_t width, const size_t height, const size_t depth)
    : array_(nullptr),
      array_ptr_(nullptr),
      width_(width),
      height_(height),
      depth_(depth) {
  CUDA_SAFE_CALL(cudaMallocPitch((void**)&array_ptr_, &pitch_,
                                 width_ * sizeof(T), height_ * depth_));

  array_ = std::shared_ptr<T>(array_ptr_, cudaFree);

  ComputeCudaConfig();
}

template <typename T>
GpuMat<T>::~GpuMat() {
  array_.reset();
  array_ptr_ = nullptr;
  pitch_ = 0;
  width_ = 0;
  height_ = 0;
  depth_ = 0;
}

template <typename T>
__host__ __device__ const T* GpuMat<T>::GetPtr() const {
  return array_ptr_;
}

template <typename T>
__host__ __device__ T* GpuMat<T>::GetPtr() {
  return array_ptr_;
}

template <typename T>
__host__ __device__ size_t GpuMat<T>::GetPitch() const {
  return pitch_;
}

template <typename T>
__host__ __device__ size_t GpuMat<T>::GetWidth() const {
  return width_;
}

template <typename T>
__host__ __device__ size_t GpuMat<T>::GetHeight() const {
  return height_;
}

template <typename T>
__host__ __device__ size_t GpuMat<T>::GetDepth() const {
  return depth_;
}

template <typename T>
__device__ T GpuMat<T>::Get(const size_t row, const size_t col) const {
  return Get(row, col, 0);
}

template <typename T>
__device__ T GpuMat<T>::Get(const size_t row, const size_t col,
                            const size_t slice) const {
  return *((T*)((char*)array_ptr_ + pitch_ * (slice * height_ + row)) + col);
}

template <typename T>
__device__ void GpuMat<T>::GetSlice(const size_t row, const size_t col,
                                    T* values) const {
  for (size_t slice = 0; slice < depth_; ++slice) {
    values[slice] = Get(row, col, slice);
  }
}

template <typename T>
__device__ T& GpuMat<T>::GetRef(const size_t row, const size_t col) {
  return GetRef(row, col, 0);
}

template <typename T>
__device__ T& GpuMat<T>::GetRef(const size_t row, const size_t col,
                                const size_t slice) {
  return *((T*)((char*)array_ptr_ + pitch_ * (slice * height_ + row)) + col);
}

template <typename T>
__device__ void GpuMat<T>::Set(const size_t row, const size_t col,
                               const T value) {
  Set(row, col, 0, value);
}

template <typename T>
__device__ void GpuMat<T>::Set(const size_t row, const size_t col,
                               const size_t slice, const T value) {
  *((T*)((char*)array_ptr_ + pitch_ * (slice * height_ + row)) + col) = value;
}

template <typename T>
__device__ void GpuMat<T>::SetSlice(const size_t row, const size_t col,
                                    const T* values) {
  for (size_t slice = 0; slice < depth_; ++slice) {
    Set(row, col, slice, values[slice]);
  }
}

template <typename T>
void GpuMat<T>::FillWithScalar(const T value) {
  cudaMemset(array_ptr_, value, width_ * height_ * depth_ * sizeof(T));
  CUDA_CHECK_ERROR();
}

template <typename T>
void GpuMat<T>::FillWithVector(const T* values) {
  T* values_device;
  cudaMalloc((void**)&values_device, depth_ * sizeof(T));
  cudaMemcpy(values_device, values, depth_ * sizeof(T), cudaMemcpyHostToDevice);
  internal::FillWithVectorKernel<T>
      <<<gridSize_, blockSize_>>>(values_device, *this);
  cudaFree(values_device);
  CUDA_CHECK_ERROR();
}

template <typename T>
void GpuMat<T>::FillWithRandomNumbers(const T min_value, const T max_value,
                                      const GpuMat<curandState> random_state) {
  internal::FillWithRandomNumbersKernel<T>
      <<<gridSize_, blockSize_>>>(*this, random_state, min_value, max_value);
  CUDA_CHECK_ERROR();
}

template <typename T>
void GpuMat<T>::CopyToDevice(const T* data, const size_t pitch) {
  CUDA_SAFE_CALL(cudaMemcpy2D((void*)array_ptr_, (size_t)pitch_, (void*)data,
                              pitch, width_ * sizeof(T), height_ * depth_,
                              cudaMemcpyHostToDevice));
}

template <typename T>
void GpuMat<T>::CopyToHost(T* data, const size_t pitch) const {
  CUDA_SAFE_CALL(cudaMemcpy2D((void*)data, pitch, (void*)array_ptr_,
                              (size_t)pitch_, width_ * sizeof(T),
                              height_ * depth_, cudaMemcpyDeviceToHost));
}

template <typename T>
Mat<T> GpuMat<T>::CopyToMat() const {
  Mat<T> mat(width_, height_, depth_);
  CopyToHost(mat.GetPtr(), mat.GetWidth() * sizeof(T));
  return mat;
}

template <typename T>
void GpuMat<T>::Transpose(GpuMat<T>* output) {
  for (size_t slice = 0; slice < depth_; ++slice) {
    CudaTranspose(array_ptr_ + slice * pitch_ / sizeof(T) * GetHeight(),
                  output->GetPtr() +
                      slice * output->pitch_ / sizeof(T) * output->GetHeight(),
                  width_, height_, pitch_, output->pitch_);
    CUDA_CHECK_ERROR();
  }
}

template <typename T>
void GpuMat<T>::FlipHorizontal(GpuMat<T>* output) {
  for (size_t slice = 0; slice < depth_; ++slice) {
    CudaFlipHorizontal(
        array_ptr_ + slice * pitch_ / sizeof(T) * GetHeight(),
        output->GetPtr() +
            slice * output->pitch_ / sizeof(T) * output->GetHeight(),
        width_, height_, pitch_, output->pitch_);
    CUDA_CHECK_ERROR();
  }
}

template <typename T>
void GpuMat<T>::Rotate(GpuMat<T>* output) {
  for (size_t slice = 0; slice < depth_; ++slice) {
    CudaRotate(array_ptr_ + slice * pitch_ / sizeof(T) * GetHeight(),
               output->GetPtr() +
                   slice * output->pitch_ / sizeof(T) * output->GetHeight(),
               width_, height_, pitch_, output->pitch_);
    CUDA_CHECK_ERROR();
  }
  // This is equivalent to the following code:
  //   GpuMat<T> flipped_array(width_, height_, GetDepth());
  //   FlipHorizontal(&flipped_array);
  //   flipped_array.Transpose(output);
}

template <typename T>
void GpuMat<T>::Read(const std::string& file_name) {
  std::fstream text_file(file_name, std::ios_base::in | std::ios_base::binary);
  CHECK(text_file.is_open()) << file_name;

  size_t width;
  size_t height;
  size_t depth;
  char unused_char;
  text_file >> width >> unused_char >> height >> unused_char >> depth >>
      unused_char;
  std::streampos pos = text_file.tellg();
  text_file.close();

  std::fstream binary_file(file_name,
                           std::ios_base::in | std::ios_base::binary);
  binary_file.seekg(pos);

  std::vector<T> source(width_ * height_ * depth_);
  binary_file.read(reinterpret_cast<char*>(source.data()),
                   width * height * depth * sizeof(T));
  binary_file.close();

  CopyToDevice(source.data(), width_ * sizeof(T));
}

template <typename T>
void GpuMat<T>::Write(const std::string& file_name) {
  std::vector<T> dest(width_ * height_ * depth_);
  CopyToHost(dest.data(), width_ * sizeof(T));

  std::fstream text_file(file_name, std::ios_base::out);
  text_file << width_ << "&" << height_ << "&" << depth_ << "&";
  text_file.close();

  std::fstream binary_file(
      file_name,
      std::ios_base::out | std::ios_base::binary | std::ios_base::app);
  binary_file.write((char*)dest.data(), sizeof(T) * width_ * height_ * depth_);
  binary_file.close();
}

template <typename T>
void GpuMat<T>::Write(const std::string& file_name, const size_t slice) {
  std::vector<T> dest(width_ * height_);
  CUDA_SAFE_CALL(cudaMemcpy2D(
      (void*)dest.data(), width_ * sizeof(T),
      (void*)(array_ptr_ + slice * height_ * pitch_ / sizeof(T)), pitch_,
      width_ * sizeof(T), height_, cudaMemcpyDeviceToHost));

  std::fstream text_file(file_name, std::ios_base::out);
  text_file << width_ << "&" << height_ << "&" << 1 << "&";
  text_file.close();

  std::fstream binary_file(
      file_name,
      std::ios_base::out | std::ios_base::binary | std::ios_base::app);

  binary_file.write((char*)dest.data(), sizeof(T) * width_ * height_);
  binary_file.close();
}

template <typename T>
void GpuMat<T>::ComputeCudaConfig() {
  blockSize_.x = kBlockDimX;
  blockSize_.y = kBlockDimY;
  blockSize_.z = 1;

  gridSize_.x = (width_ - 1) / kBlockDimX + 1;
  gridSize_.y = (height_ - 1) / kBlockDimY + 1;
  gridSize_.z = 1;
}

#endif  // __CUDACC__

}  // namespace mvs
}  // namespace colmap

#endif  // COLMAP_SRC_MVS_GPU_MAT_H_
