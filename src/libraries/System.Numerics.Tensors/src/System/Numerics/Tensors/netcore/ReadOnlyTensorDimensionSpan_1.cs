﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Runtime.CompilerServices;

#if NET9_0_OR_GREATER
using System.Collections;
using System.Collections.Generic;
#endif

namespace System.Numerics.Tensors
{
    /// <summary>Represents the slices that exist within a dimension of a tensor span.</summary>
    /// <typeparam name="T">The type of the elements within the tensor span.</typeparam>
    public readonly ref struct ReadOnlyTensorDimensionSpan<T>
    {
        private readonly ReadOnlyTensorSpan<T> _tensor;
        private readonly nint _length;
        private readonly int _dimension;
        private readonly TensorShape _sliceShape;

        internal ReadOnlyTensorDimensionSpan(ReadOnlyTensorSpan<T> tensor, int dimension)
        {
            if ((uint)dimension >= tensor.Rank)
            {
                ThrowHelper.ThrowArgumentOutOfRangeException();
            }
            dimension += 1;

            _tensor = tensor;
            _length = TensorPrimitives.Product(tensor.Lengths[..dimension]);
            _dimension = dimension;
            _sliceShape = TensorShape.Create((dimension != tensor.Rank) ? tensor.Lengths[dimension..] : [1], tensor.Strides[dimension..], tensor.IsPinned);
        }

        /// <summary>Gets <c>true</c> if the slices that exist within the tracked dimension are dense; otherwise, <c>false</c>.</summary>
        public bool IsDense => _sliceShape.IsDense;

        /// <summary>Gets the length of the tensor dimension span.</summary>
        public nint Length => _length;

        /// <summary>Gets the tensor span representing a slice of the tracked dimension using the specified index.</summary>
        /// <param name="index">The index of the tensor span slice to retrieve within the tracked dimension.</param>
        /// <returns>The tensor span representing a slice of the tracked dimension using <paramref name="index" />.</returns>
        public ReadOnlyTensorSpan<T> this[nint index]
        {
            get
            {
                if ((nuint)index >= (nuint)_length)
                {
                    ThrowHelper.ThrowArgumentOutOfRangeException();
                }

                nint linearOffset = _tensor._shape.GetLinearOffsetForDimension(index, _dimension);
                return new ReadOnlyTensorSpan<T>(ref Unsafe.Add(ref _tensor._reference, linearOffset), _sliceShape);
            }
        }

        /// <summary>Gets an enumerator for the readonly tensor dimension span.</summary>
        public Enumerator GetEnumerator() => new Enumerator(this);

        /// <summary>Enumerates the spans of a tensor dimension span.</summary>
        public ref struct Enumerator
#if NET9_0_OR_GREATER
            : IEnumerator<ReadOnlyTensorSpan<T>>
#endif
        {
            private readonly ReadOnlyTensorDimensionSpan<T> _span;
            private nint _index;

            internal Enumerator(ReadOnlyTensorDimensionSpan<T> span)
            {
                _span = span;
                _index = -1;
            }

            /// <summary>Gets the span at the current position of the enumerator.</summary>
            public readonly ReadOnlyTensorSpan<T> Current => _span[_index];

            /// <summary>Advances the enumerator to the next element of the tensor span.</summary>
            public bool MoveNext()
            {
                nint index = _index + 1;

                if (index < _span.Length)
                {
                    _index = index;
                    return true;
                }
                return false;
            }

            /// <summary>Sets the enumerator to its initial position, which is before the first element in the tensor span.</summary>
            public void Reset()
            {
                _index = -1;
            }

#if NET9_0_OR_GREATER
            //
            // IDisposable
            //

            void IDisposable.Dispose() { }

            //
            // IEnumerator
            //

            readonly object? IEnumerator.Current => throw new NotSupportedException();
#endif
        }
    }
}
