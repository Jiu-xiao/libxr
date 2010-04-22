// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2008 Gael Guennebaud <g.gael@free.fr>
// Copyright (C) 2006-2010 Benoit Jacob <jacob.benoit.1@gmail.com>
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

#ifndef EIGEN_BLOCK_H
#define EIGEN_BLOCK_H

/** \class Block
  *
  * \brief Expression of a fixed-size or dynamic-size block
  *
  * \param XprType the type of the expression in which we are taking a block
  * \param BlockRows the number of rows of the block we are taking at compile time (optional)
  * \param BlockCols the number of columns of the block we are taking at compile time (optional)
  * \param _DirectAccessStatus \internal used for partial specialization
  *
  * This class represents an expression of either a fixed-size or dynamic-size block. It is the return
  * type of DenseBase::block(int,int,int,int) and DenseBase::block<int,int>(int,int) and
  * most of the time this is the only way it is used.
  *
  * However, if you want to directly maniputate block expressions,
  * for instance if you want to write a function returning such an expression, you
  * will need to use this class.
  *
  * Here is an example illustrating the dynamic case:
  * \include class_Block.cpp
  * Output: \verbinclude class_Block.out
  *
  * \note Even though this expression has dynamic size, in the case where \a XprType
  * has fixed size, this expression inherits a fixed maximal size which means that evaluating
  * it does not cause a dynamic memory allocation.
  *
  * Here is an example illustrating the fixed-size case:
  * \include class_FixedBlock.cpp
  * Output: \verbinclude class_FixedBlock.out
  *
  * \sa DenseBase::block(int,int,int,int), DenseBase::block(int,int), class VectorBlock
  */
template<typename XprType, int BlockRows, int BlockCols, int _DirectAccessStatus>
struct ei_traits<Block<XprType, BlockRows, BlockCols, _DirectAccessStatus> > : ei_traits<XprType>
{
  typedef typename ei_traits<XprType>::Scalar Scalar;
  typedef typename ei_traits<XprType>::StorageKind StorageKind;
  typedef typename ei_traits<XprType>::XprKind XprKind;
  typedef typename ei_nested<XprType>::type XprTypeNested;
  typedef typename ei_unref<XprTypeNested>::type _XprTypeNested;
  enum{
    MatrixRows = ei_traits<XprType>::RowsAtCompileTime,
    MatrixCols = ei_traits<XprType>::ColsAtCompileTime,
    RowsAtCompileTime = MatrixRows == 0 ? 0 : BlockRows,
    ColsAtCompileTime = MatrixCols == 0 ? 0 : BlockCols,
    MaxRowsAtCompileTime = BlockRows==0 ? 0
                         : RowsAtCompileTime != Dynamic ? int(RowsAtCompileTime)
                         : int(ei_traits<XprType>::MaxRowsAtCompileTime),
    MaxColsAtCompileTime = BlockCols==0 ? 0
                         : ColsAtCompileTime != Dynamic ? int(ColsAtCompileTime)
                         : int(ei_traits<XprType>::MaxColsAtCompileTime),
    XprTypeIsRowMajor = (int(ei_traits<XprType>::Flags)&RowMajorBit) != 0,
    IsRowMajor = (MaxRowsAtCompileTime==1&&MaxColsAtCompileTime!=1) ? 1
               : (MaxColsAtCompileTime==1&&MaxRowsAtCompileTime!=1) ? 0
               : XprTypeIsRowMajor,
    HasSameStorageOrderAsXprType = (IsRowMajor == XprTypeIsRowMajor),
    InnerSize = XprTypeIsRowMajor // notice how it's XprTypeIsRowMajor here, not IsRowMajor. Inner size is computed wrt the host matrix's storage order.
              ? int(ColsAtCompileTime) : int(RowsAtCompileTime),
    InnerStrideAtCompileTime = HasSameStorageOrderAsXprType
                             ? int(ei_inner_stride_at_compile_time<XprType>::ret)
                             : int(ei_outer_stride_at_compile_time<XprType>::ret),
    OuterStrideAtCompileTime = HasSameStorageOrderAsXprType
                             ? int(ei_outer_stride_at_compile_time<XprType>::ret)
                             : int(ei_inner_stride_at_compile_time<XprType>::ret),
    MaskPacketAccessBit = (InnerSize == Dynamic || (InnerSize % ei_packet_traits<Scalar>::size) == 0)
                       && (InnerStrideAtCompileTime == 1)
                        ? PacketAccessBit : 0,
    FlagsLinearAccessBit = (RowsAtCompileTime == 1 || ColsAtCompileTime == 1) ? LinearAccessBit : 0,
    Flags0 = ei_traits<XprType>::Flags & (HereditaryBits | MaskPacketAccessBit | DirectAccessBit),
    Flags1 = Flags0 | FlagsLinearAccessBit,
    Flags = (Flags1 & ~RowMajorBit) | (IsRowMajor ? RowMajorBit : 0)
  };
};

template<typename XprType, int BlockRows, int BlockCols, int _DirectAccessStatus> class Block
  : public ei_dense_xpr_base<Block<XprType, BlockRows, BlockCols, _DirectAccessStatus> >::type
{
  public:

    typedef typename XprType::template MakeBase< Block<XprType, BlockRows, BlockCols, _DirectAccessStatus> >::Type Base;
    EIGEN_DENSE_PUBLIC_INTERFACE(Block)

    class InnerIterator;

    /** Column or Row constructor
      */
    inline Block(const XprType& xpr, int i)
      : m_xpr(xpr),
        // It is a row if and only if BlockRows==1 and BlockCols==XprType::ColsAtCompileTime,
        // and it is a column if and only if BlockRows==XprType::RowsAtCompileTime and BlockCols==1,
        // all other cases are invalid.
        // The case a 1x1 matrix seems ambiguous, but the result is the same anyway.
        m_startRow( (BlockRows==1) && (BlockCols==XprType::ColsAtCompileTime) ? i : 0),
        m_startCol( (BlockRows==XprType::RowsAtCompileTime) && (BlockCols==1) ? i : 0),
        m_blockRows(BlockRows==1 ? 1 : xpr.rows()),
        m_blockCols(BlockCols==1 ? 1 : xpr.cols())
    {
      ei_assert( (i>=0) && (
          ((BlockRows==1) && (BlockCols==XprType::ColsAtCompileTime) && i<xpr.rows())
        ||((BlockRows==XprType::RowsAtCompileTime) && (BlockCols==1) && i<xpr.cols())));
    }

    /** Fixed-size constructor
      */
    inline Block(const XprType& xpr, int startRow, int startCol)
      : m_xpr(xpr), m_startRow(startRow), m_startCol(startCol),
        m_blockRows(BlockRows), m_blockCols(BlockCols)
    {
      EIGEN_STATIC_ASSERT(RowsAtCompileTime!=Dynamic && ColsAtCompileTime!=Dynamic,THIS_METHOD_IS_ONLY_FOR_FIXED_SIZE)
      ei_assert(startRow >= 0 && BlockRows >= 1 && startRow + BlockRows <= xpr.rows()
             && startCol >= 0 && BlockCols >= 1 && startCol + BlockCols <= xpr.cols());
    }

    /** Dynamic-size constructor
      */
    inline Block(const XprType& xpr,
          int startRow, int startCol,
          int blockRows, int blockCols)
      : m_xpr(xpr), m_startRow(startRow), m_startCol(startCol),
                          m_blockRows(blockRows), m_blockCols(blockCols)
    {
      ei_assert((RowsAtCompileTime==Dynamic || RowsAtCompileTime==blockRows)
          && (ColsAtCompileTime==Dynamic || ColsAtCompileTime==blockCols));
      ei_assert(startRow >= 0 && blockRows >= 0 && startRow + blockRows <= xpr.rows()
          && startCol >= 0 && blockCols >= 0 && startCol + blockCols <= xpr.cols());
    }

    EIGEN_INHERIT_ASSIGNMENT_OPERATORS(Block)

    inline int rows() const { return m_blockRows.value(); }
    inline int cols() const { return m_blockCols.value(); }

    inline Scalar& coeffRef(int row, int col)
    {
      return m_xpr.const_cast_derived()
               .coeffRef(row + m_startRow.value(), col + m_startCol.value());
    }

    inline const CoeffReturnType coeff(int row, int col) const
    {
      return m_xpr.coeff(row + m_startRow.value(), col + m_startCol.value());
    }

    inline Scalar& coeffRef(int index)
    {
      return m_xpr.const_cast_derived()
             .coeffRef(m_startRow.value() + (RowsAtCompileTime == 1 ? 0 : index),
                       m_startCol.value() + (RowsAtCompileTime == 1 ? index : 0));
    }

    inline const CoeffReturnType coeff(int index) const
    {
      return m_xpr
             .coeff(m_startRow.value() + (RowsAtCompileTime == 1 ? 0 : index),
                    m_startCol.value() + (RowsAtCompileTime == 1 ? index : 0));
    }

    template<int LoadMode>
    inline PacketScalar packet(int row, int col) const
    {
      return m_xpr.template packet<Unaligned>
              (row + m_startRow.value(), col + m_startCol.value());
    }

    template<int LoadMode>
    inline void writePacket(int row, int col, const PacketScalar& x)
    {
      m_xpr.const_cast_derived().template writePacket<Unaligned>
              (row + m_startRow.value(), col + m_startCol.value(), x);
    }

    template<int LoadMode>
    inline PacketScalar packet(int index) const
    {
      return m_xpr.template packet<Unaligned>
              (m_startRow.value() + (RowsAtCompileTime == 1 ? 0 : index),
               m_startCol.value() + (RowsAtCompileTime == 1 ? index : 0));
    }

    template<int LoadMode>
    inline void writePacket(int index, const PacketScalar& x)
    {
      m_xpr.const_cast_derived().template writePacket<Unaligned>
         (m_startRow.value() + (RowsAtCompileTime == 1 ? 0 : index),
          m_startCol.value() + (RowsAtCompileTime == 1 ? index : 0), x);
    }

    #ifdef EIGEN_PARSED_BY_DOXYGEN
    /** \sa MapBase::data() */
    inline const Scalar* data() const;
    inline int innerStride() const;
    inline int outerStride() const;
    #endif

  protected:

    const typename XprType::Nested m_xpr;
    const ei_int_if_dynamic<XprType::RowsAtCompileTime == 1 ? 0 : Dynamic> m_startRow;
    const ei_int_if_dynamic<XprType::ColsAtCompileTime == 1 ? 0 : Dynamic> m_startCol;
    const ei_int_if_dynamic<RowsAtCompileTime> m_blockRows;
    const ei_int_if_dynamic<ColsAtCompileTime> m_blockCols;
};

/** \internal */
template<typename XprType, int BlockRows, int BlockCols>
class Block<XprType,BlockRows,BlockCols,HasDirectAccess>
  : public MapBase<Block<XprType, BlockRows, BlockCols,HasDirectAccess> >
{
  public:

    typedef MapBase<Block> Base;
    EIGEN_DENSE_PUBLIC_INTERFACE(Block)

    EIGEN_INHERIT_ASSIGNMENT_OPERATORS(Block)

    /** Column or Row constructor
      */
    inline Block(const XprType& xpr, int i)
      : Base(&xpr.const_cast_derived().coeffRef(
              (BlockRows==1) && (BlockCols==XprType::ColsAtCompileTime) ? i : 0,
              (BlockRows==XprType::RowsAtCompileTime) && (BlockCols==1) ? i : 0),
             BlockRows==1 ? 1 : xpr.rows(),
             BlockCols==1 ? 1 : xpr.cols()),
        m_xpr(xpr)
    {
      ei_assert( (i>=0) && (
          ((BlockRows==1) && (BlockCols==XprType::ColsAtCompileTime) && i<xpr.rows())
        ||((BlockRows==XprType::RowsAtCompileTime) && (BlockCols==1) && i<xpr.cols())));
    }

    /** Fixed-size constructor
      */
    inline Block(const XprType& xpr, int startRow, int startCol)
      : Base(&xpr.const_cast_derived().coeffRef(startRow,startCol)), m_xpr(xpr)
    {
      ei_assert(startRow >= 0 && BlockRows >= 1 && startRow + BlockRows <= xpr.rows()
             && startCol >= 0 && BlockCols >= 1 && startCol + BlockCols <= xpr.cols());
    }

    /** Dynamic-size constructor
      */
    inline Block(const XprType& xpr,
          int startRow, int startCol,
          int blockRows, int blockCols)
      : Base(&xpr.const_cast_derived().coeffRef(startRow,startCol), blockRows, blockCols),
        m_xpr(xpr)
    {
      ei_assert((RowsAtCompileTime==Dynamic || RowsAtCompileTime==blockRows)
             && (ColsAtCompileTime==Dynamic || ColsAtCompileTime==blockCols));
      ei_assert(startRow >= 0 && blockRows >= 0 && startRow + blockRows <= xpr.rows()
             && startCol >= 0 && blockCols >= 0 && startCol + blockCols <= xpr.cols());
    }

    /** \sa MapBase::innerStride() */
    inline int innerStride() const
    {
      return ei_traits<Block>::HasSameStorageOrderAsXprType
             ? m_xpr.innerStride()
             : m_xpr.outerStride();
    }

    /** \sa MapBase::outerStride() */
    inline int outerStride() const
    {
      return ei_traits<Block>::HasSameStorageOrderAsXprType
             ? m_xpr.outerStride()
             : m_xpr.innerStride();
    }

  #ifndef __SUNPRO_CC
  // FIXME sunstudio is not friendly with the above friend...
  // META-FIXME there is no 'friend' keyword around here. Is this obsolete?
  protected:
  #endif

    #ifndef EIGEN_PARSED_BY_DOXYGEN
    /** \internal used by allowAligned() */
    inline Block(const XprType& xpr, const Scalar* data, int blockRows, int blockCols)
      : Base(data, blockRows, blockCols), m_xpr(xpr)
    {}
    #endif

  protected:
    const typename XprType::Nested m_xpr;
};

/** \returns a dynamic-size expression of a block in *this.
  *
  * \param startRow the first row in the block
  * \param startCol the first column in the block
  * \param blockRows the number of rows in the block
  * \param blockCols the number of columns in the block
  *
  * Example: \include MatrixBase_block_int_int_int_int.cpp
  * Output: \verbinclude MatrixBase_block_int_int_int_int.out
  *
  * \note Even though the returned expression has dynamic size, in the case
  * when it is applied to a fixed-size matrix, it inherits a fixed maximal size,
  * which means that evaluating it does not cause a dynamic memory allocation.
  *
  * \sa class Block, block(int,int)
  */
template<typename Derived>
inline Block<Derived> DenseBase<Derived>
  ::block(int startRow, int startCol, int blockRows, int blockCols)
{
  return Block<Derived>(derived(), startRow, startCol, blockRows, blockCols);
}

/** This is the const version of block(int,int,int,int). */
template<typename Derived>
inline const Block<Derived> DenseBase<Derived>
  ::block(int startRow, int startCol, int blockRows, int blockCols) const
{
  return Block<Derived>(derived(), startRow, startCol, blockRows, blockCols);
}




/** \returns a dynamic-size expression of a top-right corner of *this.
  *
  * \param cRows the number of rows in the corner
  * \param cCols the number of columns in the corner
  *
  * Example: \include MatrixBase_topRightCorner_int_int.cpp
  * Output: \verbinclude MatrixBase_topRightCorner_int_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline Block<Derived> DenseBase<Derived>
  ::topRightCorner(int cRows, int cCols)
{
  return Block<Derived>(derived(), 0, cols() - cCols, cRows, cCols);
}

/** This is the const version of topRightCorner(int, int).*/
template<typename Derived>
inline const Block<Derived>
DenseBase<Derived>::topRightCorner(int cRows, int cCols) const
{
  return Block<Derived>(derived(), 0, cols() - cCols, cRows, cCols);
}

/** \returns an expression of a fixed-size top-right corner of *this.
  *
  * The template parameters CRows and CCols are the number of rows and columns in the corner.
  *
  * Example: \include MatrixBase_template_int_int_topRightCorner.cpp
  * Output: \verbinclude MatrixBase_template_int_int_topRightCorner.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int CRows, int CCols>
inline Block<Derived, CRows, CCols>
DenseBase<Derived>::topRightCorner()
{
  return Block<Derived, CRows, CCols>(derived(), 0, cols() - CCols);
}

/** This is the const version of topRightCorner<int, int>().*/
template<typename Derived>
template<int CRows, int CCols>
inline const Block<Derived, CRows, CCols>
DenseBase<Derived>::topRightCorner() const
{
  return Block<Derived, CRows, CCols>(derived(), 0, cols() - CCols);
}




/** \returns a dynamic-size expression of a top-left corner of *this.
  *
  * \param cRows the number of rows in the corner
  * \param cCols the number of columns in the corner
  *
  * Example: \include MatrixBase_topLeftCorner_int_int.cpp
  * Output: \verbinclude MatrixBase_topLeftCorner_int_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline Block<Derived> DenseBase<Derived>
  ::topLeftCorner(int cRows, int cCols)
{
  return Block<Derived>(derived(), 0, 0, cRows, cCols);
}

/** This is the const version of topLeftCorner(int, int).*/
template<typename Derived>
inline const Block<Derived>
DenseBase<Derived>::topLeftCorner(int cRows, int cCols) const
{
  return Block<Derived>(derived(), 0, 0, cRows, cCols);
}

/** \returns an expression of a fixed-size top-left corner of *this.
  *
  * The template parameters CRows and CCols are the number of rows and columns in the corner.
  *
  * Example: \include MatrixBase_template_int_int_topLeftCorner.cpp
  * Output: \verbinclude MatrixBase_template_int_int_topLeftCorner.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int CRows, int CCols>
inline Block<Derived, CRows, CCols>
DenseBase<Derived>::topLeftCorner()
{
  return Block<Derived, CRows, CCols>(derived(), 0, 0);
}

/** This is the const version of topLeftCorner<int, int>().*/
template<typename Derived>
template<int CRows, int CCols>
inline const Block<Derived, CRows, CCols>
DenseBase<Derived>::topLeftCorner() const
{
  return Block<Derived, CRows, CCols>(derived(), 0, 0);
}






/** \returns a dynamic-size expression of a bottom-right corner of *this.
  *
  * \param cRows the number of rows in the corner
  * \param cCols the number of columns in the corner
  *
  * Example: \include MatrixBase_bottomRightCorner_int_int.cpp
  * Output: \verbinclude MatrixBase_bottomRightCorner_int_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline Block<Derived> DenseBase<Derived>
  ::bottomRightCorner(int cRows, int cCols)
{
  return Block<Derived>(derived(), rows() - cRows, cols() - cCols, cRows, cCols);
}

/** This is the const version of bottomRightCorner(int, int).*/
template<typename Derived>
inline const Block<Derived>
DenseBase<Derived>::bottomRightCorner(int cRows, int cCols) const
{
  return Block<Derived>(derived(), rows() - cRows, cols() - cCols, cRows, cCols);
}

/** \returns an expression of a fixed-size bottom-right corner of *this.
  *
  * The template parameters CRows and CCols are the number of rows and columns in the corner.
  *
  * Example: \include MatrixBase_template_int_int_bottomRightCorner.cpp
  * Output: \verbinclude MatrixBase_template_int_int_bottomRightCorner.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int CRows, int CCols>
inline Block<Derived, CRows, CCols>
DenseBase<Derived>::bottomRightCorner()
{
  return Block<Derived, CRows, CCols>(derived(), rows() - CRows, cols() - CCols);
}

/** This is the const version of bottomRightCorner<int, int>().*/
template<typename Derived>
template<int CRows, int CCols>
inline const Block<Derived, CRows, CCols>
DenseBase<Derived>::bottomRightCorner() const
{
  return Block<Derived, CRows, CCols>(derived(), rows() - CRows, cols() - CCols);
}




/** \returns a dynamic-size expression of a bottom-left corner of *this.
  *
  * \param cRows the number of rows in the corner
  * \param cCols the number of columns in the corner
  *
  * Example: \include MatrixBase_bottomLeftCorner_int_int.cpp
  * Output: \verbinclude MatrixBase_bottomLeftCorner_int_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline Block<Derived> DenseBase<Derived>
  ::bottomLeftCorner(int cRows, int cCols)
{
  return Block<Derived>(derived(), rows() - cRows, 0, cRows, cCols);
}

/** This is the const version of bottomLeftCorner(int, int).*/
template<typename Derived>
inline const Block<Derived>
DenseBase<Derived>::bottomLeftCorner(int cRows, int cCols) const
{
  return Block<Derived>(derived(), rows() - cRows, 0, cRows, cCols);
}

/** \returns an expression of a fixed-size bottom-left corner of *this.
  *
  * The template parameters CRows and CCols are the number of rows and columns in the corner.
  *
  * Example: \include MatrixBase_template_int_int_bottomLeftCorner.cpp
  * Output: \verbinclude MatrixBase_template_int_int_bottomLeftCorner.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int CRows, int CCols>
inline Block<Derived, CRows, CCols>
DenseBase<Derived>::bottomLeftCorner()
{
  return Block<Derived, CRows, CCols>(derived(), rows() - CRows, 0);
}

/** This is the const version of bottomLeftCorner<int, int>().*/
template<typename Derived>
template<int CRows, int CCols>
inline const Block<Derived, CRows, CCols>
DenseBase<Derived>::bottomLeftCorner() const
{
  return Block<Derived, CRows, CCols>(derived(), rows() - CRows, 0);
}



/** \returns a block consisting of the top rows of *this.
  *
  * \param n the number of rows in the block
  *
  * Example: \include MatrixBase_topRows_int.cpp
  * Output: \verbinclude MatrixBase_topRows_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline typename DenseBase<Derived>::RowsBlockXpr DenseBase<Derived>
  ::topRows(int n)
{
  return RowsBlockXpr(derived(), 0, 0, n, cols());
}

/** This is the const version of topRows(int).*/
template<typename Derived>
inline const typename DenseBase<Derived>::RowsBlockXpr
DenseBase<Derived>::topRows(int n) const
{
  return RowsBlockXpr(derived(), 0, 0, n, cols());
}

/** \returns a block consisting of the top rows of *this.
  *
  * \param N the number of rows in the block
  *
  * Example: \include MatrixBase_template_int_topRows.cpp
  * Output: \verbinclude MatrixBase_template_int_topRows.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int N>
inline typename DenseBase<Derived>::template NRowsBlockXpr<N>::Type
DenseBase<Derived>::topRows()
{
  return typename DenseBase<Derived>::template NRowsBlockXpr<N>::Type(derived(), 0, 0, N, cols());
}

/** This is the const version of topRows<int>().*/
template<typename Derived>
template<int N>
inline const typename DenseBase<Derived>::template NRowsBlockXpr<N>::Type
DenseBase<Derived>::topRows() const
{
  return typename DenseBase<Derived>::template NRowsBlockXpr<N>::Type(derived(), 0, 0, N, cols());
}





/** \returns a block consisting of the bottom rows of *this.
  *
  * \param n the number of rows in the block
  *
  * Example: \include MatrixBase_bottomRows_int.cpp
  * Output: \verbinclude MatrixBase_bottomRows_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline typename DenseBase<Derived>::RowsBlockXpr DenseBase<Derived>
  ::bottomRows(int n)
{
  return RowsBlockXpr(derived(), rows() - n, 0, n, cols());
}

/** This is the const version of bottomRows(int).*/
template<typename Derived>
inline const typename DenseBase<Derived>::RowsBlockXpr
DenseBase<Derived>::bottomRows(int n) const
{
  return RowsBlockXpr(derived(), rows() - n, 0, n, cols());
}

/** \returns a block consisting of the bottom rows of *this.
  *
  * \param N the number of rows in the block
  *
  * Example: \include MatrixBase_template_int_bottomRows.cpp
  * Output: \verbinclude MatrixBase_template_int_bottomRows.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int N>
inline typename DenseBase<Derived>::template NRowsBlockXpr<N>::Type
DenseBase<Derived>::bottomRows()
{
  return typename NRowsBlockXpr<N>::Type(derived(), rows() - N, 0, N, cols());
}

/** This is the const version of bottomRows<int>().*/
template<typename Derived>
template<int N>
inline const typename DenseBase<Derived>::template NRowsBlockXpr<N>::Type
DenseBase<Derived>::bottomRows() const
{
  return typename NRowsBlockXpr<N>::Type(derived(), rows() - N, 0, N, cols());
}





/** \returns a block consisting of the top columns of *this.
  *
  * \param n the number of columns in the block
  *
  * Example: \include MatrixBase_leftCols_int.cpp
  * Output: \verbinclude MatrixBase_leftCols_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline typename DenseBase<Derived>::ColsBlockXpr DenseBase<Derived>
  ::leftCols(int n)
{
  return ColsBlockXpr(derived(), 0, 0, rows(), n);
}

/** This is the const version of leftCols(int).*/
template<typename Derived>
inline const typename DenseBase<Derived>::ColsBlockXpr
DenseBase<Derived>::leftCols(int n) const
{
  return ColsBlockXpr(derived(), 0, 0, rows(), n);
}

/** \returns a block consisting of the top columns of *this.
  *
  * \param N the number of columns in the block
  *
  * Example: \include MatrixBase_template_int_leftCols.cpp
  * Output: \verbinclude MatrixBase_template_int_leftCols.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int N>
inline typename DenseBase<Derived>::template NColsBlockXpr<N>::Type
DenseBase<Derived>::leftCols()
{
  return typename NColsBlockXpr<N>::Type(derived(), 0, 0, rows(), N);
}

/** This is the const version of leftCols<int>().*/
template<typename Derived>
template<int N>
inline const typename DenseBase<Derived>::template NColsBlockXpr<N>::Type
DenseBase<Derived>::leftCols() const
{
  return typename NColsBlockXpr<N>::Type(derived(), 0, 0, rows(), N);
}





/** \returns a block consisting of the top columns of *this.
  *
  * \param n the number of columns in the block
  *
  * Example: \include MatrixBase_rightCols_int.cpp
  * Output: \verbinclude MatrixBase_rightCols_int.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
inline typename DenseBase<Derived>::ColsBlockXpr DenseBase<Derived>
  ::rightCols(int n)
{
  return ColsBlockXpr(derived(), 0, cols() - n, rows(), n);
}

/** This is the const version of rightCols(int).*/
template<typename Derived>
inline const typename DenseBase<Derived>::ColsBlockXpr
DenseBase<Derived>::rightCols(int n) const
{
  return ColsBlockXpr(derived(), 0, cols() - n, rows(), n);
}

/** \returns a block consisting of the top columns of *this.
  *
  * \param N the number of columns in the block
  *
  * Example: \include MatrixBase_template_int_rightCols.cpp
  * Output: \verbinclude MatrixBase_template_int_rightCols.out
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int N>
inline typename DenseBase<Derived>::template NColsBlockXpr<N>::Type
DenseBase<Derived>::rightCols()
{
  return typename DenseBase<Derived>::template NColsBlockXpr<N>::Type(derived(), 0, cols() - N, rows(), N);
}

/** This is the const version of rightCols<int>().*/
template<typename Derived>
template<int N>
inline const typename DenseBase<Derived>::template NColsBlockXpr<N>::Type
DenseBase<Derived>::rightCols() const
{
  return typename DenseBase<Derived>::template NColsBlockXpr<N>::Type(derived(), 0, cols() - N, rows(), N);
}





/** \returns a fixed-size expression of a block in *this.
  *
  * The template parameters \a BlockRows and \a BlockCols are the number of
  * rows and columns in the block.
  *
  * \param startRow the first row in the block
  * \param startCol the first column in the block
  *
  * Example: \include MatrixBase_block_int_int.cpp
  * Output: \verbinclude MatrixBase_block_int_int.out
  *
  * \note since block is a templated member, the keyword template has to be used
  * if the matrix type is also a template parameter: \code m.template block<3,3>(1,1); \endcode
  *
  * \sa class Block, block(int,int,int,int)
  */
template<typename Derived>
template<int BlockRows, int BlockCols>
inline Block<Derived, BlockRows, BlockCols>
DenseBase<Derived>::block(int startRow, int startCol)
{
  return Block<Derived, BlockRows, BlockCols>(derived(), startRow, startCol);
}

/** This is the const version of block<>(int, int). */
template<typename Derived>
template<int BlockRows, int BlockCols>
inline const Block<Derived, BlockRows, BlockCols>
DenseBase<Derived>::block(int startRow, int startCol) const
{
  return Block<Derived, BlockRows, BlockCols>(derived(), startRow, startCol);
}

/** \returns an expression of the \a i-th column of *this. Note that the numbering starts at 0.
  *
  * Example: \include MatrixBase_col.cpp
  * Output: \verbinclude MatrixBase_col.out
  *
  * \sa row(), class Block */
template<typename Derived>
inline typename DenseBase<Derived>::ColXpr
DenseBase<Derived>::col(int i)
{
  return ColXpr(derived(), i);
}

/** This is the const version of col(). */
template<typename Derived>
inline const typename DenseBase<Derived>::ColXpr
DenseBase<Derived>::col(int i) const
{
  return ColXpr(derived(), i);
}

/** \returns an expression of the \a i-th row of *this. Note that the numbering starts at 0.
  *
  * Example: \include MatrixBase_row.cpp
  * Output: \verbinclude MatrixBase_row.out
  *
  * \sa col(), class Block */
template<typename Derived>
inline typename DenseBase<Derived>::RowXpr
DenseBase<Derived>::row(int i)
{
  return RowXpr(derived(), i);
}

/** This is the const version of row(). */
template<typename Derived>
inline const typename DenseBase<Derived>::RowXpr
DenseBase<Derived>::row(int i) const
{
  return RowXpr(derived(), i);
}

#endif // EIGEN_BLOCK_H
