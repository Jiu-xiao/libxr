// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2016
// Mehdi Goli    Codeplay Software Ltd.
// Ralph Potter  Codeplay Software Ltd.
// Luke Iwanski  Codeplay Software Ltd.
// Contact: <eigen@codeplay.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#define EIGEN_TEST_NO_LONGDOUBLE
#define EIGEN_TEST_NO_COMPLEX
#define EIGEN_TEST_FUNC cxx11_tensor_device_sycl
#define EIGEN_DEFAULT_DENSE_INDEX_TYPE int
#define EIGEN_USE_SYCL

#include "main.h"
#include <unsupported/Eigen/CXX11/Tensor>
#include<stdint.h>

template <typename DataType, int DataLayout>
void test_device_sycl(const Eigen::SyclDevice &sycl_device) {
  std::cout <<"Hello from ComputeCpp: the requested device exists and the device name is : "
    << sycl_device.sycl_queue().get_device(). template get_info<cl::sycl::info::device::name>() <<std::endl;
  int sizeDim1 = 100;
  array<int, 1> tensorRange = {{sizeDim1}};
  Tensor<DataType, 1, DataLayout> in(tensorRange);
  Tensor<DataType, 1, DataLayout> in1(tensorRange);
  memset(in1.data(), 1,in1.size()*sizeof(DataType));
  DataType * gpu_in_data  = static_cast<DataType*>(sycl_device.allocate(in.size()*sizeof(DataType)));
  sycl_device.memset(gpu_in_data, 1,in.size()*sizeof(DataType) );
  sycl_device.memcpyDeviceToHost(in.data(), gpu_in_data, in.size()*sizeof(DataType) );
  for (int i=0; i<in.size(); i++) {
    VERIFY_IS_APPROX(in(i), in1(i));
  }
  sycl_device.deallocate(gpu_in_data);
}

template <typename DataType, int DataLayout>
void test_device_exceptions(const Eigen::SyclDevice &sycl_device) {
  VERIFY(sycl_device.ok());
  int sizeDim1 = 100;
  array<int, 1> tensorDims = {{sizeDim1}};
  DataType* gpu_data = static_cast<DataType*>(sycl_device.allocate(sizeDim1*sizeof(DataType)));
  TensorMap<Tensor<DataType, 1,DataLayout>> in(gpu_data, tensorDims);
  TensorMap<Tensor<DataType, 1,DataLayout>> out(gpu_data, tensorDims);

  out.device(sycl_device) = in / in.constant(0);
  VERIFY(!sycl_device.ok());
  sycl_device.deallocate(gpu_data);
}

template<typename DataType, typename dev_Selector> void sycl_device_test_per_device(dev_Selector s){
  QueueInterface queueInterface(s);
  auto sycl_device = Eigen::SyclDevice(&queueInterface);
  test_device_sycl<DataType, RowMajor>(sycl_device);
  test_device_sycl<DataType, ColMajor>(sycl_device);
  /// this test throw an exeption. enable it if you want to see the exception
  // test_device_exceptions<DataType, RowMajor>(sycl_device);
  /// this test throw an exeption. enable it if you want to see the exception
  // test_device_exceptions<DataType, ColMajor>(sycl_device);

}

void test_cxx11_tensor_device_sycl() {
  printf("Test on GPU: OpenCL\n");
  CALL_SUBTEST(sycl_device_test_per_device<int>((cl::sycl::gpu_selector())));
  printf("repeating the test on CPU: OpenCL\n");
  CALL_SUBTEST(sycl_device_test_per_device<int>((cl::sycl::cpu_selector())));
  printf("repeating the test on CPU: HOST\n");
  CALL_SUBTEST(sycl_device_test_per_device<int>((cl::sycl::host_selector())));
  printf("Test Passed******************\n" );
}
