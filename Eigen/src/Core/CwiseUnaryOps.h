
#ifndef EIGEN_PARSED_BY_DOXYGEN

/** \internal Represents a scalar multiple of a matrix */
typedef CwiseUnaryOp<ei_scalar_multiple_op<Scalar>, Derived> ScalarMultipleReturnType;
/** \internal Represents a quotient of a matrix by a scalar*/
typedef CwiseUnaryOp<ei_scalar_quotient1_op<Scalar>, Derived> ScalarQuotient1ReturnType;
/** \internal the return type of MatrixBase::conjugate() */
typedef typename ei_meta_if<NumTraits<Scalar>::IsComplex,
                    const CwiseUnaryOp<ei_scalar_conjugate_op<Scalar>, Derived>,
                    const Derived&
                  >::ret ConjugateReturnType;
/** \internal the return type of MatrixBase::real() const */
typedef typename ei_meta_if<NumTraits<Scalar>::IsComplex,
                    const CwiseUnaryOp<ei_scalar_real_op<Scalar>, Derived>,
                    const Derived&
                  >::ret RealReturnType;
/** \internal the return type of MatrixBase::real() */
typedef typename ei_meta_if<NumTraits<Scalar>::IsComplex,
                    CwiseUnaryView<ei_scalar_real_op<Scalar>, Derived>,
                    Derived&
                  >::ret NonConstRealReturnType;
/** \internal the return type of MatrixBase::imag() const */
typedef CwiseUnaryOp<ei_scalar_imag_op<Scalar>, Derived> ImagReturnType;
/** \internal the return type of MatrixBase::imag() */
typedef CwiseUnaryView<ei_scalar_imag_op<Scalar>, Derived> NonConstImagReturnType;

#endif // not EIGEN_PARSED_BY_DOXYGEN

/** \returns an expression of the opposite of \c *this
  */
EIGEN_STRONG_INLINE const CwiseUnaryOp<ei_scalar_opposite_op<typename ei_traits<Derived>::Scalar>,Derived>
operator-() const { return derived(); }

EIGEN_STRONG_INLINE Derived& operator*=(const Scalar& other)
{ return *this = *this * other; }
EIGEN_STRONG_INLINE Derived& operator/=(const Scalar& other)
{ return *this = *this / other; }

/** \returns an expression of \c *this scaled by the scalar factor \a scalar */
EIGEN_STRONG_INLINE const ScalarMultipleReturnType
operator*(const Scalar& scalar) const
{
  return CwiseUnaryOp<ei_scalar_multiple_op<Scalar>, Derived>
    (derived(), ei_scalar_multiple_op<Scalar>(scalar));
}

#ifdef EIGEN_PARSED_BY_DOXYGEN
const ScalarMultipleReturnType operator*(const RealScalar& scalar) const;
#endif

/** \returns an expression of \c *this divided by the scalar value \a scalar */
EIGEN_STRONG_INLINE const CwiseUnaryOp<ei_scalar_quotient1_op<typename ei_traits<Derived>::Scalar>, Derived>
operator/(const Scalar& scalar) const
{
  return CwiseUnaryOp<ei_scalar_quotient1_op<Scalar>, Derived>
    (derived(), ei_scalar_quotient1_op<Scalar>(scalar));
}

/** Overloaded for efficient real matrix times complex scalar value */
EIGEN_STRONG_INLINE const CwiseUnaryOp<ei_scalar_multiple2_op<Scalar,std::complex<Scalar> >, Derived>
operator*(const std::complex<Scalar>& scalar) const
{
  return CwiseUnaryOp<ei_scalar_multiple2_op<Scalar,std::complex<Scalar> >, Derived>
    (*static_cast<const Derived*>(this), ei_scalar_multiple2_op<Scalar,std::complex<Scalar> >(scalar));
}

inline friend const ScalarMultipleReturnType
operator*(const Scalar& scalar, const StorageBaseType& matrix)
{ return matrix*scalar; }

inline friend const CwiseUnaryOp<ei_scalar_multiple2_op<Scalar,std::complex<Scalar> >, Derived>
operator*(const std::complex<Scalar>& scalar, const StorageBaseType& matrix)
{ return matrix*scalar; }

/** \returns an expression of *this with the \a Scalar type casted to
  * \a NewScalar.
  *
  * The template parameter \a NewScalar is the type we are casting the scalars to.
  *
  * \sa class CwiseUnaryOp
  */
template<typename NewType>
typename ei_cast_return_type<Derived,const CwiseUnaryOp<ei_scalar_cast_op<typename ei_traits<Derived>::Scalar, NewType>, Derived> >::type
cast() const
{
  return derived();
}

/** \returns an expression of the complex conjugate of \c *this.
  *
  * \sa adjoint() */
EIGEN_STRONG_INLINE ConjugateReturnType
conjugate() const
{
  return ConjugateReturnType(derived());
}

/** \returns a read-only expression of the real part of \c *this.
  *
  * \sa imag() */
EIGEN_STRONG_INLINE RealReturnType
real() const { return derived(); }

/** \returns an read-only expression of the imaginary part of \c *this.
  *
  * \sa real() */
EIGEN_STRONG_INLINE const ImagReturnType
imag() const { return derived(); }

/** \returns an expression of a custom coefficient-wise unary operator \a func of *this
  *
  * The template parameter \a CustomUnaryOp is the type of the functor
  * of the custom unary operator.
  *
  * Example:
  * \include class_CwiseUnaryOp.cpp
  * Output: \verbinclude class_CwiseUnaryOp.out
  *
  * \sa class CwiseUnaryOp, class CwiseBinarOp, MatrixBase::operator-, Cwise::abs
  */
template<typename CustomUnaryOp>
EIGEN_STRONG_INLINE const CwiseUnaryOp<CustomUnaryOp, Derived>
unaryExpr(const CustomUnaryOp& func = CustomUnaryOp()) const
{
  return CwiseUnaryOp<CustomUnaryOp, Derived>(derived(), func);
}

/** \returns an expression of a custom coefficient-wise unary operator \a func of *this
  *
  * The template parameter \a CustomUnaryOp is the type of the functor
  * of the custom unary operator.
  *
  * Example:
  * \include class_CwiseUnaryOp.cpp
  * Output: \verbinclude class_CwiseUnaryOp.out
  *
  * \sa class CwiseUnaryOp, class CwiseBinarOp, MatrixBase::operator-, Cwise::abs
  */
template<typename CustomViewOp>
EIGEN_STRONG_INLINE const CwiseUnaryView<CustomViewOp, Derived>
unaryViewExpr(const CustomViewOp& func = CustomViewOp()) const
{
  return CwiseUnaryView<CustomViewOp, Derived>(derived(), func);
}

/** \returns a non const expression of the real part of \c *this.
  *
  * \sa imag() */
EIGEN_STRONG_INLINE NonConstRealReturnType
real() { return derived(); }

/** \returns a non const expression of the imaginary part of \c *this.
  *
  * \sa real() */
EIGEN_STRONG_INLINE NonConstImagReturnType
imag() { return derived(); }

/** \returns an expression of the coefficient-wise absolute value of \c *this
  *
  * Example: \include MatrixBase_cwiseAbs.cpp
  * Output: \verbinclude MatrixBase_cwiseAbs.out
  *
  * \sa cwiseAbs2()
  */
EIGEN_STRONG_INLINE const CwiseUnaryOp<ei_scalar_abs_op<Scalar>,Derived>
cwiseAbs() const { return derived(); }

/** \returns an expression of the coefficient-wise squared absolute value of \c *this
  *
  * Example: \include MatrixBase_cwiseAbs2.cpp
  * Output: \verbinclude MatrixBase_cwiseAbs2.out
  *
  * \sa cwiseAbs()
  */
EIGEN_STRONG_INLINE const CwiseUnaryOp<ei_scalar_abs2_op<Scalar>,Derived>
cwiseAbs2() const { return derived(); }

/** \returns an expression of the coefficient-wise square root of *this.
  *
  * Example: \include MatrixBase_cwiseSqrt.cpp
  * Output: \verbinclude MatrixBase_cwiseSqrt.out
  *
  * \sa cwisePow(), cwiseSquare()
  */
inline const CwiseUnaryOp<ei_scalar_sqrt_op<Scalar>,Derived>
cwiseSqrt() const { return derived(); }

/** \returns an expression of the coefficient-wise inverse of *this.
  *
  * Example: \include MatrixBase_cwiseInverse.cpp
  * Output: \verbinclude MatrixBase_cwiseInverse.out
  *
  * \sa cwiseProduct()
  */
inline const CwiseUnaryOp<ei_scalar_inverse_op<Scalar>,Derived>
cwiseInverse() const { return derived(); }

/** \returns an expression of the coefficient-wise == operator of \c *this and a scalar \a s
  *
  * \warning this performs an exact comparison, which is generally a bad idea with floating-point types.
  * In order to check for equality between two vectors or matrices with floating-point coefficients, it is
  * generally a far better idea to use a fuzzy comparison as provided by MatrixBase::isApprox() and
  * MatrixBase::isMuchSmallerThan().
  *
  * \sa cwiseEqual(const MatrixBase<OtherDerived> &) const
  */
inline const CwiseUnaryOp<std::binder1st<std::equal_to<Scalar> >,Derived>
cwiseEqual(Scalar s) const
{
  return CwiseUnaryOp<std::binder1st<std::equal_to<Scalar> >,Derived>
          (derived(), std::bind1st(std::equal_to<Scalar>(), s));
}
