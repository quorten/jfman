/* A simple expandable array implementation.

Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015, 2017 Andrew Makousky

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

/* How to use exparray:

* This expandable array implementation is divided into two sections:
  core primitives and convenience functions.

* Whenever you want to use a specific type in an expandable array,
  first call `EA_TYPE(typename)' with your type.  This macro defines a
  structure customized to be an array of that specific type.  The
  first field of the structure, `d', is a pointer of the given type,
  which points to the dynamically allocated array contents.

* When you want to declare an expandable array, simply use
  `typename_array' as the type name (with your type as `typename', of
  course).

* Initialize the array before use by calling `EA_INIT()'.  `array' is
  the value of your array (use the syntax `*array' if it is a
  pointer), and `reserve' is the number of elements to allocate space
  for in advance (usually you should pick 16).  If linear reallocation
  is being used, then the array will be reallocated with that many
  more elements when the array becomes full.

* All of these macros accept values rather than pointers, so use
  syntax as described above if you have a pointer that will be used as
  an argument.

* NOTE: Because of the fact that this expandable array implementation
  uses macros extensively, there are some programming situations where
  macro inputs get clobbered or evaluated twice during intermediate
  execution of the macros.  So far, this code was only modified in two
  places to compensate for these issues.  But then again, using macros
  can put you as a programmer at an advantage by very tricking writing
  of expressions such as `EA_INSERT(array, 0, array.d[array.num])'...
  but maybe you don't want to be that tricky.

* When you are done using an expandable array, make sure you call
  `EA_DESTROY()'.

* To reserve space for extra elements that will be written in-place at
  a later time, set `typename_array::len' to the desired size and then
  call `EA_NORMALIZE()'.  `EA_NORMALIZE()' can also be used to free up
  unused space in an array.

* The normal behavior of an exparray is to allocate a reserve in
  advance, and allocate more space once all of the previously
  pre-allocated space is filled up.  Thus, one way to append a single
  element to the array is to write to the position at
  `typename_array::d[typename_array::len]', then call `EA_ADD()'.

* You can change which functions are used for allocation by defining
  `ea_malloc', `ea_realloc', and `ea_free' to the functions to use for
  allocation.

* You can also modify allocation behavior by providing different
  macros for `EA_GROW()' and `EA_NORMALIZE()'.  Normally, allocation
  behavior is done in an exponential manner without clearing newly
  allocated memory.  You can change this by defining
  `EA_LINEAR_REALLOC' or `EA_GARRAY_REALLOC'.

* The rest of the functions are convenience functions that use the
  previously mentioned primitives.  See their individual documentation
  for more details.

* There is a wrapper header available for you to use this
  implementation as a backend in place of the real GLib GArray
  implementation.  */

#ifndef EXPARRAY_H
#define EXPARRAY_H

#include <string.h>

#ifndef ea_malloc
#include "xmalloc.h"
#define ea_malloc  xmalloc
#define ea_realloc xrealloc
#define ea_free    free /* Don't need extra safety */
#endif

/* Courtesy of GLib et al.  (The idea originated from lots of earlier
 * software, but I don't remember them all.)
 *
 * Provide simple macro statement wrappers:
 *   EA_STMT_START { statements; } EA_STMT_END;
 * This can be used as a single statement, like:
 *   if (x) EA_STMT_START { ... } EA_STMT_END; else ...
 * This intentionally does not use compiler extensions like GCC's
 * '({...})' to avoid portability issues or side effects when compiled
 * with different compilers.
 */
#define EA_STMT_START do
#define EA_STMT_END while (0)

/* This neat trick can be used to determine the element size of an
   expandable array type without requiring the user to explicitly
   specify the type name or size.  Of course, the array must have a
   pointer type indicative of the element size.  */
#define EA_TYSIZE(array) sizeof(*((array).d))

/* However, under circumstances when sufficient type information is
   not available, the user may opt to include the `tysize' member in
   the structures by defining EA_USE_TYSIZE.  */
#ifdef EA_USE_TYSIZE
#define EA_DEF_TYSIZE unsigned tysize;
#else
#define EA_DEF_TYSIZE
#endif

#define EA_TYPE(typename)						\
	struct typename##_array_tag					\
	{											\
		typename *d;							\
		unsigned len;							\
		EA_DEF_TYSIZE							\
		/* User-defined fields.  */				\
		unsigned user1;							\
	};											\
	typedef struct typename##_array_tag typename##_array

/* In order to avoid typecasting problems in C++, a generic array type
   must be defined for internal use by exparray.  */
struct generic_array_tag
{
	unsigned char *d;
	unsigned len;
	EA_DEF_TYSIZE
	/* User-defined fields.  */
	unsigned user1;
};
typedef struct generic_array_tag generic_array;

/* Aliases for user-defined fields.  */
#define ea_len_alloc user1

#ifdef EA_USE_TYSIZE
#define EA_INIT_TYSIZE (array).tysize = EA_TYSIZE(array);
#else
#define EA_INIT_TYSIZE
#endif

/* Initialize the given array.

   Parameters:

   `array'       the value (not pointer) of the array to modify
   `reserve'     the amount of memory to pre-allocate, typically 16.

   The array's "length" is always initialized to zero.  */
#define EA_INIT(array, reserve)									\
EA_STMT_START {													\
	generic_array *cpp_punk = (generic_array *)(&array);		\
	(array).len = 0;											\
	EA_INIT_TYSIZE												\
	(array).ea_len_alloc = reserve;								\
	cpp_punk->d = (unsigned char *)								\
		ea_malloc(EA_TYSIZE(array) * (array).ea_len_alloc);		\
} EA_STMT_END

#ifdef EA_USE_TYSIZE
#define EA_DESTROY_TYSIZE (array).tysize = 0;
#else
#define EA_DESTROY_TYSIZE
#endif

/* Destroy the given array.  This is mostly just a convenience
   function.  */
#define EA_DESTROY(array)							\
EA_STMT_START {										\
	if ((array).d != NULL)							\
		ea_free((array).d);							\
	(array).d = NULL;								\
	(array).len = 0;								\
	EA_DESTROY_TYSIZE								\
	(array).user1 = 0;								\
} EA_STMT_END

/* Reallocators:

   EA_GROW(array)
     Reallocation function for single step growth.  The `len' member
     of the array should have already been incremented.

   EA_NORMALIZE(array)
     Ensure that the allocated space is consistent with the current
     allocation algorithm.  */

/* GLib GArray reallocators.  */
#ifdef EA_GARRAY_REALLOC
#define EA_GROW(array)													\
EA_STMT_START {															\
	struct _GRealWrap *reala = (struct _GRealWrap *)(&array);			\
	if (reala->len + (reala->zero_term ? 1 : 0) >= reala->ea_len_alloc) \
	{																	\
		reala->ea_len_alloc <<= 1;										\
		reala->data = (guchar *)ea_realloc(reala->data, reala->tysize *	\
										  reala->ea_len_alloc);			\
		if (reala->clear)												\
		{																\
			memset(reala->data + reala->tysize * reala->len, 0,			\
				   reala->tysize * (reala->ea_len_alloc - reala->len)); \
		}																\
		else if (reala->zero_term)										\
		{																\
			memset(reala->data + reala->tysize * reala->len, 0,			\
				   reala->tysize * 1);									\
		}																\
	}																	\
} EA_STMT_END
#define EA_NORMALIZE(array)											\
EA_STMT_START {														\
	struct _GRealWrap *reala = (struct _GRealWrap *)(&array);		\
	while (reala->ea_len_alloc >= reala->len + (reala->zero_term ? 1 : 0)) \
		reala->ea_len_alloc >>= 1;									\
	if ((reala)->ea_len_alloc == 0)									\
		(reala)->ea_len_alloc = 1;									\
	while (reala->len + (reala->zero_term ? 1 : 0) >= reala->ea_len_alloc) \
		reala->ea_len_alloc <<= 1;									\
	reala->data = (guchar *)ea_realloc(reala->data, reala->tysize *	\
									  reala->ea_len_alloc);			\
	if (reala->clear)												\
	{																\
		memset(reala->data + reala->tysize * reala->len, 0,			\
			   reala->tysize * (reala->ea_len_alloc - reala->len)); \
	}																\
	else if (reala->zero_term)										\
	{																\
		memset(reala->data + reala->tysize * reala->len, 0,			\
			   reala->tysize * 1);									\
	}																\
} EA_STMT_END
/* END EA_GARRAY_REALLOC */

/* Linear reallocators.  */
#elif EA_LINEAR_REALLOC
#define ea_stride user1 /* User-defined field alias */
#define EA_GROW(array)													\
EA_STMT_START {															\
	generic_array *cpp_punk = (generic_array *)(&array);				\
	if ((array).len % (array).ea_stride == 0)							\
		cpp_punk->d = (unsigned char *)									\
			ea_realloc((array).d, EA_TYSIZE(array) *					\
					   ((array).len + (array).ea_stride));				\
} EA_STMT_END
#define EA_NORMALIZE(array)												\
EA_STMT_START {															\
	generic_array *cpp_punk = (generic_array *)(&array);				\
	cpp_punk->d = (unsigned char *)										\
		ea_realloc((array).d, EA_TYSIZE(array) *						\
	   ((array).len + ((array).ea_stride - (array).len % (array).ea_stride))); \
} EA_STMT_END
/* END EA_LINEAR_REALLOC */

/* Default exponential reallocators.  */
#else
#define EA_GROW(array)							\
EA_STMT_START {									\
	if ((array).len >= (array).ea_len_alloc)	\
	{											\
		generic_array *cpp_punk = (generic_array *)(&array);			\
		(array).ea_len_alloc <<= 1;										\
		cpp_punk->d = (unsigned char *)									\
			ea_realloc((array).d, EA_TYSIZE(array) *					\
					   (array).ea_len_alloc);							\
	}																	\
} EA_STMT_END
#define EA_NORMALIZE(array)											\
EA_STMT_START {														\
	generic_array *cpp_punk = (generic_array *)(&array);			\
	while ((array).ea_len_alloc > 0 &&								\
		   (array).ea_len_alloc >= (array).len)						\
		(array).ea_len_alloc >>= 1;									\
	if ((array).ea_len_alloc == 0)									\
		(array).ea_len_alloc = 1;									\
	while ((array).len >= (array).ea_len_alloc)						\
		(array).ea_len_alloc <<= 1;									\
	cpp_punk->d = (unsigned char *)									\
		ea_realloc((array).d, EA_TYSIZE(array) *					\
				   (array).ea_len_alloc);							\
} EA_STMT_END
#endif /* END reallocators */

/*********************************************************************
   The few necessary primitive functions were specified above.  Next
   follows the convenience functions.  */

/* The notation (array).d[x] was previously used in these helper
   functions, but after the GLib GArray wrapper was created, that
   mechanism is no longer safe.  Use this "safe referencing" macro
   instead.  */
#ifdef EA_GARRAY_REALLOC
#define EA_SR(array, x) (*((array).d + (array).tysize * (x)))
#else
#define EA_SR(array, x) (array).d[x]
#endif

/* Increment the size of the array and allocate space for one new item
   in the array.  The expected calling convention of this form is to
   first set the value of the new element in the array by doing
   something like `array.d[array.num] = value;', then calling
   `EA_ADD()'.  Expandable arrays always reserve space for one item
   beyond the current size of the array.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify */
#define EA_ADD(array)							\
EA_STMT_START {									\
	(array).len++;								\
	EA_GROW(array);								\
} EA_STMT_END

/* Increment the size of the array, allocate space for one new item,
   and move elements in the array to make space for inserting an item
   at the given position.  The expected calling convention of this
   form is similar to that of `EA_ADD()', only the value of the item
   should be set after this call rather than before it.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `pos'         the zero-based index of where the new item will be
                 inserted */
#define EA_INS(array, pos)						\
EA_STMT_START {									\
	EA_ADD(array);														\
	memmove(&EA_SR(array, pos+1), &EA_SR(array, pos),					\
			EA_TYSIZE(array) * ((array).len - pos));					\
} EA_STMT_END

/* Appends the given item to the end of the array.  Note that the
   appended item cannot be larger than an integer type.  To append a
   larger item, use `EA_APPEND_MULT()' with the quantity set to one.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `element'     the value of the item to append */
#define EA_APPEND(array, element)										\
EA_STMT_START {															\
	EA_SR(array, (array).len) = element;								\
	EA_ADD(array);														\
} EA_STMT_END

/* Inserts the given item at the indicated position.  Elements after
   this position will get moved back.  Note that the inserted item
   cannot be larger than an integer type.  To insert a larger item,
   use `EA_INSERT_MULT()' with the quantity set to one.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `pos'         the zero-based index indicating where to insert the
                 given element
   `element'     the value of the item to append */
#define EA_INSERT(array, pos, element)									\
EA_STMT_START {															\
	unsigned sos = pos; /* avoid macro evaluation problems */			\
	EA_INS(array, sos);													\
	EA_SR(array, sos) = element;										\
} EA_STMT_END

/* Append multiple items at one time.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `data'        the address of the items to append
   `num_len'     the number of items to append */
#define EA_APPEND_MULT(array, data, num_data)							\
EA_STMT_START {															\
	if (data != NULL)													\
	{																	\
		unsigned pos = (array).len;										\
		(array).len += num_data;										\
		EA_NORMALIZE(array);											\
		memcpy(&EA_SR(array, pos), data, EA_TYSIZE(array) * num_data);	\
	}																	\
} EA_STMT_END

/* Insert multiple items at one time.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `pos'         the zero-based index indicating where to insert the
                 given element
   `data'        the address of the items to insert
   `num_data'    the number of items to insert */
#define EA_INSERT_MULT(array, pos, data, num_data)						\
EA_STMT_START {															\
	if (data != NULL)													\
	{																	\
		unsigned sos = pos; /* avoid macro evaluation problems */		\
		(array).len += num_data;										\
		EA_NORMALIZE(array);											\
		memmove(&EA_SR(array, sos+num_data), &EA_SR(array, sos),		\
				EA_TYSIZE(array) * ((array).len - sos));				\
		memcpy(&EA_SR(array, sos), data, EA_TYSIZE(array) * num_data);	\
	}																	\
} EA_STMT_END

/* Prepend the given element at the beginning of the array.  Note that
   the prepended item cannot be larger than an integer type.  To
   prepend a larger item, use `EA_PREPEND_MULT()' with the quantity
   set to one.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `element'     the value of the element to append */
#define EA_PREPEND(array, element) \
	EA_INSERT(array, 0, element)

/* Prepend multiple elements at one time.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `data'        the address of the elements to prepend
   `num_data'    the number of items to prepend */
#define EA_PREPEND_MULT(array, data, num_data)	\
	EA_INSERT_MULT(array, 0, data, num_data)

/* Delete the indexed element by moving following elements over the
   indexed element.  The allocated array memory is not shrunken.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `pos'         the zero-based index indicating which element to
                 remove */
#define EA_REMOVE(array, pos)											\
EA_STMT_START {															\
	memmove(&EA_SR(array, pos), &EA_SR(array, pos+1),					\
			EA_TYSIZE(array) * ((array).len - (pos + 1)));				\
	(array).len--;														\
} EA_STMT_END

/* Delete the indexed element by moving the last element into the
   indexed position.  The allocated array memory is not shrunken.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `pos'         the zero-based index indicating which element to
                 remove */
#define EA_REMOVE_FAST(array, pos)										\
EA_STMT_START {															\
	memcpy(&EA_SR(array, pos), &EA_SR(array, (array).len-1),			\
		   EA_TYSIZE(array));											\
	(array).len--;														\
} EA_STMT_END

/* Remove multiple consecutive of elements by moving the following
   elements over the deleted elements.  The allocated array memory is
   not shrunken.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `data'        the address of the data array to append
   `pos'         the zero-based index indicating the first element to
                 remove
   `num_data'    the number of items to remove */
#define EA_REMOVE_MULT(array, pos, num_data)							\
EA_STMT_START {															\
	memmove(&EA_SR(array, pos), &EA_SR(array, pos+num_data),			\
			EA_TYSIZE(array) * ((array).len - (pos + num_data)));		\
	(array).len -= num_data;											\
} EA_STMT_END

/* Set an array's size and allocate enough memory for that size.

   Parameters:

   `array'       the value (not pointer) of the exparray to modify
   `size'        the new size of the array, measured in elements */
#define EA_SET_SIZE(array, size)					\
EA_STMT_START {										\
	(array).len = size;								\
	EA_NORMALIZE(array);							\
} EA_STMT_END

/* Push an element onto the end of the given array, as if it were a
   stack.  This function is just an alias for `EA_APPEND()'.  */
#define EA_PUSH_BACK(array, element) \
	EA_APPEND(array, element)

/* Pop an element off of the end of the given array, as if it were a
   stack.  The allocated array memory is not shrunken.  */
#define EA_POP_BACK(array) \
	(array).len--

/* This function is just an alias for `EA_PREPEND()'.  You should not
   use this macro for a stack data structure, as it will have serious
   performance problems when used in that way.  */
#define EA_PUSH_FRONT(array, element) \
	EA_PREPEND(array, element)

/* Remove an element from the beginning of the given array.  The
   allocated array memory is not shrunken.  You should not use this
   macro for a stack data structure, as it will have serious
   performance problems when used in that way.  */
#define EA_POP_FRONT(array) \
	EA_REMOVE(array, 0)

/* Remove all elements starting at a zero-based index and going until
   the end of the array.  This is just a convenience function for
   `EA_REMOVE_MULT()'.  */
#define EA_ERASEP(array, pos) \
	EA_REMOVE_MULT(array, pos, (array).len - pos)

/* Remove a range of elements using a zero-based index to the first
   element to remove and a zero-based index just beyond the last
   element to remove.  */
#define EA_ERASER(array, begin, end) \
	EA_REMOVE_MULT(array, begin, end - begin)

/* "Rename" exparrays to be arrays based off of the opposite array's
   data elements.  */
#define EA_SWAP(typename, left, right)				\
EA_STMT_START {										\
	typename##_array temp;							\
	memcpy(temp, left, sizeof(typename##_array));	\
	memcpy(left, right, sizeof(typename##_array));	\
	memcpy(right, temp, sizeof(typename##_array));	\
} EA_STMT_END

/* Get the element at the beginning of the array.  */
#define EA_FRONT(array) EA_SR(array, 0)

/* Get the element at the end of the array.  */
#define EA_BACK(array) \
	EA_SR(array, ((array).len == 0 ? 0 : (array).len - 1))

/* Get an index to the first element of the array.  */
#define EA_BEGIN(array) 0

/* Get an index just beyond the last element of the array.  */
#define EA_END(array) ((array).len)

/* Clear the contents of an array.  The allocated space is not
   shrunken.  */
#define EA_CLEAR(array) ((array).len = 0)

/* Test if an array is empty.  This is just an alias for testing
   whether the length is zero.  */
#define EA_EMPTY(array) ((array).len == 0)

#endif /* not EXPARRAY_H */
