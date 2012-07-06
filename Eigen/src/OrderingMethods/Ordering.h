 
// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2012  Désiré Nuentsa-Wakam <desire.nuentsa_wakam@inria.fr>
//
// Eigen is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or (at your option) any later version.
//
// Alternatively, you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of
// the License, or (at your option) any later version.
//
// Eigen is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License or the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License and a copy of the GNU General Public License along with
// Eigen. If not, see <http://www.gnu.org/licenses/>.

#ifndef EIGEN_ORDERING_H
#define EIGEN_ORDERING_H

#include "Amd.h"
namespace Eigen {
namespace internal {
    
    /**
    * Get the symmetric pattern A^T+A from the input matrix A. 
    * FIXME: The values should not be considered here
    */
    template<typename MatrixType> 
    void ordering_helper_at_plus_a(const MatrixType& mat, MatrixType& symmat)
    {
      MatrixType C;
      C = mat.transpose(); // NOTE: Could be  costly
      for (int i = 0; i < C.rows(); i++) 
      {
          for (typename MatrixType::InnerIterator it(C, i); it; ++it)
            it.valueRef() = 0.0;
      }
      symmat = C + mat; 
    }
    
}

/** 
 * Get the approximate minimum degree ordering
 * If the matrix is not structurally symmetric, an ordering of A^T+A is computed
 * \tparam  Index The type of indices of the matrix 
 */
template <typename Index>
class AMDOrdering
{
  public:
    typedef PermutationMatrix<Dynamic, Dynamic, Index> PermutationType;
    
    /** Compute the permutation vector from a sparse matrix
     * This routine is much faster if the input matrix is column-major     
     */
    template <typename MatrixType>
    void operator()(const MatrixType& mat, PermutationType& perm)
    {
      // Compute the symmetric pattern
      SparseMatrix<typename MatrixType::Scalar, ColMajor, Index> symm;
      internal::ordering_helper_at_plus_a(mat,symm); 
    
      // Call the AMD routine 
      //m_mat.prune(keep_diag());
      internal::minimum_degree_ordering(symm, perm);
    }
    
    /** Compute the permutation with a selfadjoint matrix */
    template <typename SrcType, unsigned int SrcUpLo> 
    void operator()(const SparseSelfAdjointView<SrcType, SrcUpLo>& mat, PermutationType& perm)
    { 
      SparseMatrix<typename SrcType::Scalar, ColMajor, Index> C = mat;
      
      // Call the AMD routine 
      // m_mat.prune(keep_diag()); //Remove the diagonal elements 
      internal::minimum_degree_ordering(C, perm);
    }
};

/** 
 * Get the natural ordering
 * 
 *NOTE Returns an empty permutation matrix
 * \tparam  Index The type of indices of the matrix 
 */
template <typename Index>
class NaturalOrdering
{
  public:
    typedef PermutationMatrix<Dynamic, Dynamic, Index> PermutationType;
    
    /** Compute the permutation vector from a column-major sparse matrix */
    template <typename MatrixType>
    void operator()(const MatrixType& mat, PermutationType& perm)
    {
      perm.resize(0); 
    }
    
};

/** 
 * Get the column approximate minimum degree ordering 
 * The matrix should be in column-major format
 */
// template<typename Scalar, typename Index>
// class COLAMDOrdering: public OrderingBase< ColamdOrdering<Scalar, Index> >
// {
//   public:
//     typedef OrderingBase< ColamdOrdering<Scalar, Index> > Base;
//     typedef SparseMatrix<Scalar,ColMajor,Index> MatrixType;  
//     
//   public:
//     COLAMDOrdering():Base() {}
//     
//     COLAMDOrdering(const MatrixType& matrix):Base()
//     {
//       compute(matrix);
//     }
//     COLAMDOrdering(const MatrixType& mat, PermutationType& perm_c):Base()
//     {
//       compute(matrix); 
//       perm_c = this.get_perm();
//     }
//     void compute(const MatrixType& mat)
//     {
//       // Test if the matrix is column major...
//           
//       int m = mat.rows();
//       int n = mat.cols();
//       int nnz = mat.nonZeros();
//       // Get the recommended value of Alen to be used by colamd
//       int Alen = colamd_recommended(nnz, m, n); 
//       // Set the default parameters
//       double knobs[COLAMD_KNOBS]; 
//       colamd_set_defaults(knobs);
//       
//       int info;
//       VectorXi p(n), A(nnz); 
//       for(int i=0; i < n; i++) p(i) = mat.outerIndexPtr()(i);
//       for(int i=0; i < nnz; i++) A(i) = mat.innerIndexPtr()(i);
//       // Call Colamd routine to compute the ordering 
//       info = colamd(m, n, Alen, A,p , knobs, stats)
//       eigen_assert( (info != FALSE)&& "COLAMD failed " );
//       
//       m_P.resize(n);
//       for (int i = 0; i < n; i++) m_P(p(i)) = i;
//       m_isInitialized = true;
//     }
//   protected:
//     using Base::m_isInitialized;
//     using Base m_P; 
// };

} // end namespace Eigen
#endif