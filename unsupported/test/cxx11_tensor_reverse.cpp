// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2014 Navdeep Jaitly <ndjaitly@google.com and
//                    Benoit Steiner <benoit.steiner.goog@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "main.h"

#include <Eigen/CXX11/Tensor>

using Eigen::Tensor;
using Eigen::array;

template <int DataLayout>
static void test_simple_reverse()
{
  Tensor<float, 4, DataLayout> tensor(2,3,5,7);
  tensor.setRandom();

  array<bool, 4> dim_rev;
  dim_rev[0] = false;
  dim_rev[1] = true;
  dim_rev[2] = true;
  dim_rev[3] = false;

  Tensor<float, 4, DataLayout> reversed_tensor;
  reversed_tensor = tensor.reverse(dim_rev);

  VERIFY_IS_EQUAL(reversed_tensor.dimension(0), 2);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(1), 3);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(2), 5);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(3), 7);

  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 7; ++l) {
          VERIFY_IS_EQUAL(tensor(i,j,k,l), reversed_tensor(i,2-j,4-k,l));
        }
      }
    }
  }

  dim_rev[0] = true;
  dim_rev[1] = false;
  dim_rev[2] = false;
  dim_rev[3] = false;

  reversed_tensor = tensor.reverse(dim_rev);

  VERIFY_IS_EQUAL(reversed_tensor.dimension(0), 2);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(1), 3);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(2), 5);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(3), 7);


  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 7; ++l) {
          VERIFY_IS_EQUAL(tensor(i,j,k,l), reversed_tensor(1-i,j,k,l));
        }
      }
    }
  }

  dim_rev[0] = true;
  dim_rev[1] = false;
  dim_rev[2] = false;
  dim_rev[3] = true;

  reversed_tensor = tensor.reverse(dim_rev);

  VERIFY_IS_EQUAL(reversed_tensor.dimension(0), 2);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(1), 3);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(2), 5);
  VERIFY_IS_EQUAL(reversed_tensor.dimension(3), 7);


  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 7; ++l) {
          VERIFY_IS_EQUAL(tensor(i,j,k,l), reversed_tensor(1-i,j,k,6-l));
        }
      }
    }
  }
}


template <int DataLayout>
static void test_expr_reverse()
{
  Tensor<float, 4, DataLayout> tensor(2,3,5,7);
  tensor.setRandom();

  array<bool, 4> dim_rev;
  dim_rev[0] = false;
  dim_rev[1] = true;
  dim_rev[2] = false;
  dim_rev[3] = true;


  Tensor<float, 4, DataLayout> expected;
  expected = tensor.reverse(dim_rev);

  Tensor<float, 4, DataLayout> result(2,3,5,7);

  array<ptrdiff_t, 4> src_slice_dim{{2,3,1,7}};
  array<ptrdiff_t, 4> src_slice_start{{0,0,0,0}};
  array<ptrdiff_t, 4> dst_slice_dim{{2,3,1,7}};
  array<ptrdiff_t, 4> dst_slice_start{{0,0,0,0}};

  for (int i = 0; i < 5; ++i) {
    result.slice(dst_slice_start, dst_slice_dim) =
        tensor.slice(src_slice_start, src_slice_dim).reverse(dim_rev);
    src_slice_start[2] += 1;
    dst_slice_start[2] += 1;
  }

  VERIFY_IS_EQUAL(result.dimension(0), 2);
  VERIFY_IS_EQUAL(result.dimension(1), 3);
  VERIFY_IS_EQUAL(result.dimension(2), 5);
  VERIFY_IS_EQUAL(result.dimension(3), 7);

  for (int i = 0; i < expected.dimension(0); ++i) {
    for (int j = 0; j < expected.dimension(1); ++j) {
      for (int k = 0; k < expected.dimension(2); ++k) {
        for (int l = 0; l < expected.dimension(3); ++l) {
          VERIFY_IS_EQUAL(result(i,j,k,l), expected(i,j,k,l));
        }
      }
    }
  }

  dst_slice_start[2] = 0;
  result.setRandom();
  for (int i = 0; i < 5; ++i) {
    result.slice(dst_slice_start, dst_slice_dim) =
        tensor.reverse(dim_rev).slice(dst_slice_start, dst_slice_dim);
    dst_slice_start[2] += 1;
  }

  for (int i = 0; i < expected.dimension(0); ++i) {
    for (int j = 0; j < expected.dimension(1); ++j) {
      for (int k = 0; k < expected.dimension(2); ++k) {
        for (int l = 0; l < expected.dimension(3); ++l) {
          VERIFY_IS_EQUAL(result(i,j,k,l), expected(i,j,k,l));
        }
      }
    }
  }
}


void test_cxx11_tensor_reverse()
{
  CALL_SUBTEST(test_simple_reverse<ColMajor>());
  CALL_SUBTEST(test_simple_reverse<RowMajor>());
  CALL_SUBTEST(test_expr_reverse<ColMajor>());
  CALL_SUBTEST(test_expr_reverse<RowMajor>());
}
