// This file is part of Eigen, a lightweight C++ template library
// for linear algebra. Eigen itself is part of the KDE project.
//
// Copyright (C) 2006-2008 Benoit Jacob <jacob@math.jussieu.fr>
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

#ifndef EIGEN_DOT_H
#define EIGEN_DOT_H

/***************************************************************************
* Part 1 : the logic deciding a strategy for vectorization and unrolling
***************************************************************************/

template<typename Derived1, typename Derived2>
struct ei_dot_traits
{
public:
  enum {
    Vectorization = (int(Derived1::Flags)&int(Derived2::Flags)&ActualPacketAccessBit)
                 && (int(Derived1::Flags)&int(Derived2::Flags)&LinearAccessBit)
                  ? LinearVectorization
                  : NoVectorization
  };

private:
  typedef typename Derived1::Scalar Scalar;
  enum {
    PacketSize = ei_packet_traits<Scalar>::size,
    Cost = Derived1::SizeAtCompileTime * (Derived1::CoeffReadCost + Derived2::CoeffReadCost + NumTraits<Scalar>::MulCost)
           + (Derived1::SizeAtCompileTime-1) * NumTraits<Scalar>::AddCost,
    UnrollingLimit = EIGEN_UNROLLING_LIMIT * (int(Vectorization) == int(NoVectorization) ? 1 : int(PacketSize))
  };

public:
  enum {
    Unrolling = Cost <= UnrollingLimit
              ? CompleteUnrolling
              : NoUnrolling
  };
};

/***************************************************************************
* Part 2 : unrollers
***************************************************************************/

/*** no vectorization ***/

template<typename Derived1, typename Derived2, int Start, int Length>
struct ei_dot_novec_unroller
{
  enum {
    HalfLength = Length/2
  };

  typedef typename Derived1::Scalar Scalar;

  inline static Scalar run(const Derived1& v1, const Derived2& v2)
  {
    return ei_dot_novec_unroller<Derived1, Derived2, Start, HalfLength>::run(v1, v2)
         + ei_dot_novec_unroller<Derived1, Derived2, Start+HalfLength, Length-HalfLength>::run(v1, v2);
  }
};

template<typename Derived1, typename Derived2, int Start>
struct ei_dot_novec_unroller<Derived1, Derived2, Start, 1>
{
  typedef typename Derived1::Scalar Scalar;

  inline static Scalar run(const Derived1& v1, const Derived2& v2)
  {
    return v1.coeff(Start) * ei_conj(v2.coeff(Start));
  }
};

/*** vectorization ***/

template<typename Derived1, typename Derived2, int Index, int Stop,
         bool LastPacket = (Stop-Index == ei_packet_traits<typename Derived1::Scalar>::size)>
struct ei_dot_vec_unroller
{
  typedef typename Derived1::Scalar Scalar;
  typedef typename ei_packet_traits<Scalar>::type PacketScalar;

  enum {
    row1 = Derived1::RowsAtCompileTime == 1 ? 0 : Index,
    col1 = Derived1::RowsAtCompileTime == 1 ? Index : 0,
    row2 = Derived2::RowsAtCompileTime == 1 ? 0 : Index,
    col2 = Derived2::RowsAtCompileTime == 1 ? Index : 0
  };

  inline static PacketScalar run(const Derived1& v1, const Derived2& v2)
  {
    return ei_pmadd(
      v1.template packet<Aligned>(row1, col1),
      v2.template packet<Aligned>(row2, col2),
      ei_dot_vec_unroller<Derived1, Derived2, Index+ei_packet_traits<Scalar>::size, Stop>::run(v1, v2)
    );
  }
};

template<typename Derived1, typename Derived2, int Index, int Stop>
struct ei_dot_vec_unroller<Derived1, Derived2, Index, Stop, true>
{
  enum {
    row1 = Derived1::RowsAtCompileTime == 1 ? 0 : Index,
    col1 = Derived1::RowsAtCompileTime == 1 ? Index : 0,
    row2 = Derived2::RowsAtCompileTime == 1 ? 0 : Index,
    col2 = Derived2::RowsAtCompileTime == 1 ? Index : 0,
    alignment1 = (Derived1::Flags & AlignedBit) ? Aligned : Unaligned,
    alignment2 = (Derived2::Flags & AlignedBit) ? Aligned : Unaligned
  };

  typedef typename Derived1::Scalar Scalar;
  typedef typename ei_packet_traits<Scalar>::type PacketScalar;

  inline static PacketScalar run(const Derived1& v1, const Derived2& v2)
  {
    return ei_pmul(v1.template packet<alignment1>(row1, col1), v2.template packet<alignment2>(row2, col2));
  }
};

/***************************************************************************
* Part 3 : implementation of all cases
***************************************************************************/

template<typename Derived1, typename Derived2,
         int Vectorization = ei_dot_traits<Derived1, Derived2>::Vectorization,
         int Unrolling = ei_dot_traits<Derived1, Derived2>::Unrolling
>
struct ei_dot_impl;

template<typename Derived1, typename Derived2>
struct ei_dot_impl<Derived1, Derived2, NoVectorization, NoUnrolling>
{
  typedef typename Derived1::Scalar Scalar;
  static Scalar run(const Derived1& v1, const Derived2& v2)
  {
    Scalar res;
    res = v1.coeff(0) * ei_conj(v2.coeff(0));
    for(int i = 1; i < v1.size(); i++)
      res += v1.coeff(i) * ei_conj(v2.coeff(i));
    return res;
  }
};

template<typename Derived1, typename Derived2>
struct ei_dot_impl<Derived1, Derived2, NoVectorization, CompleteUnrolling>
  : public ei_dot_novec_unroller<Derived1, Derived2, 0, Derived1::SizeAtCompileTime>
{};

template<typename Derived1, typename Derived2>
struct ei_dot_impl<Derived1, Derived2, LinearVectorization, NoUnrolling>
{
  typedef typename Derived1::Scalar Scalar;
  typedef typename ei_packet_traits<Scalar>::type PacketScalar;

  static Scalar run(const Derived1& v1, const Derived2& v2)
  {
    const int size = v1.size();
    const int packetSize = ei_packet_traits<Scalar>::size;
    const int alignedSize = (size/packetSize)*packetSize;
    const int alignment1 = (Derived1::Flags & AlignedBit) ? Aligned : Unaligned;
    const int alignment2 = (Derived2::Flags & AlignedBit) ? Aligned : Unaligned;
    Scalar res;

    // do the vectorizable part of the sum
    if(size >= packetSize)
    {
      PacketScalar packet_res = ei_pmul(
                                  v1.template packet<alignment1>(0),
                                  v2.template packet<alignment2>(0)
                                );
      for(int index = packetSize; index<alignedSize; index += packetSize)
      {
        packet_res = ei_pmadd(
                       v1.template packet<alignment1>(index),
                       v2.template packet<alignment2>(index),
                       packet_res
                     );
      }
      res = ei_predux(packet_res);

      // now we must do the rest without vectorization.
      if(alignedSize == size) return res;
    }
    else // too small to vectorize anything.
         // since this is dynamic-size hence inefficient anyway for such small sizes, don't try to optimize.
    {
      res = Scalar(0);
    }

    // do the remainder of the vector
    for(int index = alignedSize; index < size; index++)
    {
      res += v1.coeff(index) * v2.coeff(index);
    }

    return res;
  }
};

template<typename Derived1, typename Derived2>
struct ei_dot_impl<Derived1, Derived2, LinearVectorization, CompleteUnrolling>
{
  typedef typename Derived1::Scalar Scalar;
  static Scalar run(const Derived1& v1, const Derived2& v2)
  {
    return ei_predux(
      ei_dot_vec_unroller<Derived1, Derived2, 0, Derived1::SizeAtCompileTime>::run(v1, v2)
    );
  }
};

/***************************************************************************
* Part 4 : implementation of MatrixBase methods
***************************************************************************/

/** \returns the dot product of *this with other.
  *
  * \only_for_vectors
  *
  * \note If the scalar type is complex numbers, then this function returns the hermitian
  * (sesquilinear) dot product, linear in the first variable and anti-linear in the
  * second variable.
  *
  * \sa norm2(), norm()
  */
template<typename Derived>
template<typename OtherDerived>
typename ei_traits<Derived>::Scalar
MatrixBase<Derived>::dot(const MatrixBase<OtherDerived>& other) const
{
  typedef typename Derived::Nested Nested;
  typedef typename OtherDerived::Nested OtherNested;
  typedef typename ei_unref<Nested>::type _Nested;
  typedef typename ei_unref<OtherNested>::type _OtherNested;

  EIGEN_STATIC_ASSERT_VECTOR_ONLY(_Nested);
  EIGEN_STATIC_ASSERT_VECTOR_ONLY(_OtherNested);
  EIGEN_STATIC_ASSERT_SAME_VECTOR_SIZE(_Nested,_OtherNested);
  ei_assert(size() == other.size());

  return ei_dot_impl<_Nested, _OtherNested>::run(derived(), other.derived());
}

/** \returns the squared norm of *this, i.e. the dot product of *this with itself.
  *
  * \only_for_vectors
  *
  * \sa dot(), norm()
  */
template<typename Derived>
inline typename NumTraits<typename ei_traits<Derived>::Scalar>::Real MatrixBase<Derived>::norm2() const
{
  return ei_real(dot(*this));
}

/** \returns the norm of *this, i.e. the square root of the dot product of *this with itself.
  *
  * \only_for_vectors
  *
  * \sa dot(), norm2()
  */
template<typename Derived>
inline typename NumTraits<typename ei_traits<Derived>::Scalar>::Real MatrixBase<Derived>::norm() const
{
  return ei_sqrt(norm2());
}

/** \returns an expression of the quotient of *this by its own norm.
  *
  * \only_for_vectors
  *
  * \sa norm(), normalize()
  */
template<typename Derived>
inline const typename MatrixBase<Derived>::ScalarQuotient1ReturnType
MatrixBase<Derived>::normalized() const
{
  return *this / norm();
}

/** Normalizes the vector, i.e. divides it by its own norm.
  *
  * \only_for_vectors
  *
  * \sa norm(), normalized()
  */
template<typename Derived>
inline void MatrixBase<Derived>::normalize()
{
  *this /= norm();
}

/** \returns true if *this is approximately orthogonal to \a other,
  *          within the precision given by \a prec.
  *
  * Example: \include MatrixBase_isOrthogonal.cpp
  * Output: \verbinclude MatrixBase_isOrthogonal.out
  */
template<typename Derived>
template<typename OtherDerived>
bool MatrixBase<Derived>::isOrthogonal
(const MatrixBase<OtherDerived>& other, RealScalar prec) const
{
  typename ei_nested<Derived,2>::type nested(derived());
  typename ei_nested<OtherDerived,2>::type otherNested(other.derived());
  return ei_abs2(nested.dot(otherNested)) <= prec * prec * nested.norm2() * otherNested.norm2();
}

/** \returns true if *this is approximately an unitary matrix,
  *          within the precision given by \a prec. In the case where the \a Scalar
  *          type is real numbers, a unitary matrix is an orthogonal matrix, whence the name.
  *
  * \note This can be used to check whether a family of vectors forms an orthonormal basis.
  *       Indeed, \c m.isUnitary() returns true if and only if the columns (equivalently, the rows) of m form an
  *       orthonormal basis.
  *
  * Example: \include MatrixBase_isUnitary.cpp
  * Output: \verbinclude MatrixBase_isUnitary.out
  */
template<typename Derived>
bool MatrixBase<Derived>::isUnitary(RealScalar prec) const
{
  typename Derived::Nested nested(derived());
  for(int i = 0; i < cols(); i++)
  {
    if(!ei_isApprox(nested.col(i).norm2(), static_cast<Scalar>(1), prec))
      return false;
    for(int j = 0; j < i; j++)
      if(!ei_isMuchSmallerThan(nested.col(i).dot(nested.col(j)), static_cast<Scalar>(1), prec))
        return false;
  }
  return true;
}
#endif // EIGEN_DOT_H
