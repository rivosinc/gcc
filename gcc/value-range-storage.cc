/* Support routines for vrange storage.
   Copyright (C) 2022-2023 Free Software Foundation, Inc.
   Contributed by Aldy Hernandez <aldyh@redhat.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "ssa.h"
#include "tree-pretty-print.h"
#include "fold-const.h"
#include "gimple-range.h"
#include "value-range-storage.h"

// Generic memory allocator to share one interface between GC and
// obstack allocators.

class vrange_internal_alloc
{
public:
  vrange_internal_alloc () { }
  virtual ~vrange_internal_alloc () { }
  virtual void *alloc (size_t size) = 0;
  virtual void free (void *) = 0;
private:
  DISABLE_COPY_AND_ASSIGN (vrange_internal_alloc);
};

class vrange_obstack_alloc final: public vrange_internal_alloc
{
public:
  vrange_obstack_alloc ()
  {
    obstack_init (&m_obstack);
  }
  virtual ~vrange_obstack_alloc () final override
  {
    obstack_free (&m_obstack, NULL);
  }
  virtual void *alloc (size_t size) final override
  {
    return obstack_alloc (&m_obstack, size);
  }
  virtual void free (void *) final override { }
private:
  obstack m_obstack;
};

class vrange_ggc_alloc final: public vrange_internal_alloc
{
public:
  vrange_ggc_alloc () { }
  virtual ~vrange_ggc_alloc () final override { }
  virtual void *alloc (size_t size) final override
  {
    return ggc_internal_alloc (size);
  }
  virtual void free (void *p) final override
  {
    return ggc_free (p);
  }
};

vrange_allocator::vrange_allocator (bool gc)
{
  if (gc)
    m_alloc = new vrange_ggc_alloc;
  else
    m_alloc = new vrange_obstack_alloc;
}

vrange_allocator::~vrange_allocator ()
{
  delete m_alloc;
}

void *
vrange_allocator::alloc (size_t size)
{
  return m_alloc->alloc (size);
}

void
vrange_allocator::free (void *p)
{
  m_alloc->free (p);
}

// Allocate a new vrange_storage object initialized to R and return
// it.

vrange_storage *
vrange_allocator::clone (const vrange &r)
{
  return vrange_storage::alloc (*m_alloc, r);
}

vrange_storage *
vrange_allocator::clone_varying (tree type)
{
  if (irange::supports_p (type))
    return irange_storage::alloc (*m_alloc, int_range <1> (type));
  if (frange::supports_p (type))
    return frange_storage::alloc (*m_alloc, frange (type));
  return NULL;
}

vrange_storage *
vrange_allocator::clone_undefined (tree type)
{
  if (irange::supports_p (type))
    return irange_storage::alloc (*m_alloc, int_range<1> ());
  if (frange::supports_p (type))
    return frange_storage::alloc  (*m_alloc, frange ());
  return NULL;
}

// Allocate a new vrange_storage object initialized to R and return
// it.  Return NULL if R is unsupported.

vrange_storage *
vrange_storage::alloc (vrange_internal_alloc &allocator, const vrange &r)
{
  if (is_a <irange> (r))
    return irange_storage::alloc (allocator, as_a <irange> (r));
  if (is_a <frange> (r))
    return frange_storage::alloc (allocator, as_a <frange> (r));
  return NULL;
}

// Set storage to R.

void
vrange_storage::set_vrange (const vrange &r)
{
  if (is_a <irange> (r))
    {
      irange_storage *s = static_cast <irange_storage *> (this);
      gcc_checking_assert (s->fits_p (as_a <irange> (r)));
      s->set_irange (as_a <irange> (r));
    }
  else if (is_a <frange> (r))
    {
      frange_storage *s = static_cast <frange_storage *> (this);
      gcc_checking_assert (s->fits_p (as_a <frange> (r)));
      s->set_frange (as_a <frange> (r));
    }
  else
    gcc_unreachable ();
}

// Restore R from storage.

void
vrange_storage::get_vrange (vrange &r, tree type) const
{
  if (is_a <irange> (r))
    {
      const irange_storage *s = static_cast <const irange_storage *> (this);
      s->get_irange (as_a <irange> (r), type);
    }
  else if (is_a <frange> (r))
    {
      const frange_storage *s = static_cast <const frange_storage *> (this);
      s->get_frange (as_a <frange> (r), type);
    }
  else
    gcc_unreachable ();
}

// Return TRUE if storage can fit R.

bool
vrange_storage::fits_p (const vrange &r) const
{
  if (is_a <irange> (r))
    {
      const irange_storage *s = static_cast <const irange_storage *> (this);
      return s->fits_p (as_a <irange> (r));
    }
  if (is_a <frange> (r))
    {
      const frange_storage *s = static_cast <const frange_storage *> (this);
      return s->fits_p (as_a <frange> (r));
    }
  gcc_unreachable ();
  return false;
}

// Return TRUE if the range in storage is equal to R.

bool
vrange_storage::equal_p (const vrange &r, tree type) const
{
  if (is_a <irange> (r))
    {
      const irange_storage *s = static_cast <const irange_storage *> (this);
      return s->equal_p (as_a <irange> (r), type);
    }
  if (is_a <frange> (r))
    {
      const frange_storage *s = static_cast <const frange_storage *> (this);
      return s->equal_p (as_a <frange> (r), type);
    }
  gcc_unreachable ();
}

//============================================================================
// irange_storage implementation
//============================================================================

unsigned char *
irange_storage::write_lengths_address ()
{
  return (unsigned char *)&m_val[(m_num_ranges * 2 + 1)
				 * WIDE_INT_MAX_HWIS (m_precision)];
}

const unsigned char *
irange_storage::lengths_address () const
{
  return const_cast <irange_storage *> (this)->write_lengths_address ();
}

// Allocate a new irange_storage object initialized to R.

irange_storage *
irange_storage::alloc (vrange_internal_alloc &allocator, const irange &r)
{
  size_t size = irange_storage::size (r);
  irange_storage *p = static_cast <irange_storage *> (allocator.alloc (size));
  new (p) irange_storage (r);
  return p;
}

// Initialize the storage with R.

irange_storage::irange_storage (const irange &r)
  : m_max_ranges (r.num_pairs ())
{
  m_num_ranges = m_max_ranges;
  set_irange (r);
}

static inline void
write_wide_int (HOST_WIDE_INT *&val, unsigned char *&len, const wide_int &w)
{
  *len = w.get_len ();
  for (unsigned i = 0; i < *len; ++i)
    *val++ = w.elt (i);
  ++len;
}

// Store R into the current storage.

void
irange_storage::set_irange (const irange &r)
{
  gcc_checking_assert (fits_p (r));

  if (r.undefined_p ())
    {
      m_kind = VR_UNDEFINED;
      return;
    }
  if (r.varying_p ())
    {
      m_kind = VR_VARYING;
      return;
    }

  m_precision = TYPE_PRECISION (r.type ());
  m_num_ranges = r.num_pairs ();
  m_kind = VR_RANGE;

  HOST_WIDE_INT *val = &m_val[0];
  unsigned char *len = write_lengths_address ();

  for (unsigned i = 0; i < r.num_pairs (); ++i)
    {
      write_wide_int (val, len, r.lower_bound (i));
      write_wide_int (val, len, r.upper_bound (i));
    }
  write_wide_int (val, len, r.m_nonzero_mask);

  if (flag_checking)
    {
      int_range_max tmp;
      get_irange (tmp, r.type ());
      gcc_checking_assert (tmp == r);
    }
}

static inline void
read_wide_int (wide_int &w,
	       const HOST_WIDE_INT *val, unsigned char len, unsigned prec)
{
  trailing_wide_int_storage stow (prec, &len,
				  const_cast <HOST_WIDE_INT *> (val));
  w = trailing_wide_int (stow);
}

// Restore a range of TYPE from storage into R.

void
irange_storage::get_irange (irange &r, tree type) const
{
  if (m_kind == VR_UNDEFINED)
    {
      r.set_undefined ();
      return;
    }
  if (m_kind == VR_VARYING)
    {
      r.set_varying (type);
      return;
    }

  gcc_checking_assert (TYPE_PRECISION (type) == m_precision);
  const HOST_WIDE_INT *val = &m_val[0];
  const unsigned char *len = lengths_address ();

  // Handle the common case where R can fit the new range.
  if (r.m_max_ranges >= m_num_ranges)
    {
      r.m_kind = VR_RANGE;
      r.m_num_ranges = m_num_ranges;
      r.m_type = type;
      for (unsigned i = 0; i < m_num_ranges * 2; ++i)
	{
	  read_wide_int (r.m_base[i], val, *len, m_precision);
	  val += *len++;
	}
    }
  // Otherwise build the range piecewise.
  else
    {
      r.set_undefined ();
      for (unsigned i = 0; i < m_num_ranges; ++i)
	{
	  wide_int lb, ub;
	  read_wide_int (lb, val, *len, m_precision);
	  val += *len++;
	  read_wide_int (ub, val, *len, m_precision);
	  val += *len++;
	  int_range<1> tmp (type, lb, ub);
	  r.union_ (tmp);
	}
    }
  read_wide_int (r.m_nonzero_mask, val, *len, m_precision);
  if (r.m_kind == VR_VARYING)
    r.m_kind = VR_RANGE;

  if (flag_checking)
    r.verify_range ();
}

bool
irange_storage::equal_p (const irange &r, tree type) const
{
  if (m_kind == VR_UNDEFINED || r.undefined_p ())
    return m_kind == r.m_kind;
  if (m_kind == VR_VARYING || r.varying_p ())
    return m_kind == r.m_kind && types_compatible_p (r.type (), type);

  tree rtype = r.type ();
  if (!types_compatible_p (rtype, type))
    return false;

  // ?? We could make this faster by doing the comparison in place,
  // without going through get_irange.
  int_range_max tmp;
  get_irange (tmp, rtype);
  return tmp == r;
}

// Return the size in bytes to allocate storage that can hold R.

size_t
irange_storage::size (const irange &r)
{
  if (r.undefined_p ())
    return sizeof (irange_storage);

  unsigned prec = TYPE_PRECISION (r.type ());
  unsigned n = r.num_pairs () * 2 + 1;
  unsigned hwi_size = ((n * WIDE_INT_MAX_HWIS (prec) - 1)
		       * sizeof (HOST_WIDE_INT));
  unsigned len_size = n;
  return sizeof (irange_storage) + hwi_size + len_size;
}

// Return TRUE if R fits in the current storage.

bool
irange_storage::fits_p (const irange &r) const
{
  return m_max_ranges >= r.num_pairs ();
}

void
irange_storage::dump () const
{
  fprintf (stderr, "irange_storage (prec=%d, ranges=%d):\n",
	   m_precision, m_num_ranges);

  if (m_num_ranges == 0)
    return;

  const HOST_WIDE_INT *val = &m_val[0];
  const unsigned char *len = lengths_address ();
  int i, j;

  fprintf (stderr, "  lengths = [ ");
  for (i = 0; i < m_num_ranges * 2 + 1; ++i)
    fprintf (stderr, "%d ", len[i]);
  fprintf (stderr, "]\n");

  for (i = 0; i < m_num_ranges; ++i)
    {
      for (j = 0; j < *len; ++j)
	fprintf (stderr, "  [PAIR %d] LB " HOST_WIDE_INT_PRINT_DEC "\n", i,
		 *val++);
      ++len;
      for (j = 0; j < *len; ++j)
	fprintf (stderr, "  [PAIR %d] UB " HOST_WIDE_INT_PRINT_DEC "\n", i,
		 *val++);
      ++len;
    }
  for (j = 0; j < *len; ++j)
    fprintf (stderr, "  [NZ] " HOST_WIDE_INT_PRINT_DEC "\n", *val++);
}

DEBUG_FUNCTION void
debug (const irange_storage &storage)
{
  storage.dump ();
  fprintf (stderr, "\n");
}

//============================================================================
// frange_storage implementation
//============================================================================

// Allocate a new frange_storage object initialized to R.

frange_storage *
frange_storage::alloc (vrange_internal_alloc &allocator, const frange &r)
{
  size_t size = sizeof (frange_storage);
  frange_storage *p = static_cast <frange_storage *> (allocator.alloc (size));
  new (p) frange_storage (r);
  return p;
}

void
frange_storage::set_frange (const frange &r)
{
  gcc_checking_assert (fits_p (r));

  m_kind = r.m_kind;
  m_min = r.m_min;
  m_max = r.m_max;
  m_pos_nan = r.m_pos_nan;
  m_neg_nan = r.m_neg_nan;
}

void
frange_storage::get_frange (frange &r, tree type) const
{
  gcc_checking_assert (r.supports_type_p (type));

  // Handle explicit NANs.
  if (m_kind == VR_NAN)
    {
      if (HONOR_NANS (type))
	{
	  if (m_pos_nan && m_neg_nan)
	    r.set_nan (type);
	  else
	    r.set_nan (type, m_neg_nan);
	}
      else
	r.set_undefined ();
      return;
    }
  if (m_kind == VR_UNDEFINED)
    {
      r.set_undefined ();
      return;
    }

  // We use the constructor to create the new range instead of writing
  // out the bits into the frange directly, because the global range
  // being read may be being inlined into a function with different
  // restrictions as when it was originally written.  We want to make
  // sure the resulting range is canonicalized correctly for the new
  // consumer.
  r = frange (type, m_min, m_max, m_kind);

  // The constructor will set the NAN bits for HONOR_NANS, but we must
  // make sure to set the NAN sign if known.
  if (HONOR_NANS (type) && (m_pos_nan ^ m_neg_nan) == 1)
    r.update_nan (m_neg_nan);
  else if (!m_pos_nan && !m_neg_nan)
    r.clear_nan ();
}

bool
frange_storage::equal_p (const frange &r, tree type) const
{
  if (r.undefined_p ())
    return m_kind == VR_UNDEFINED;

  tree rtype = type;
  if (!types_compatible_p (rtype, type))
    return false;

  frange tmp;
  get_frange (tmp, rtype);
  return tmp == r;
}

bool
frange_storage::fits_p (const frange &) const
{
  return true;
}

static vrange_allocator ggc_vrange_allocator (true);

vrange_storage *ggc_alloc_vrange_storage (tree type)
{
  return ggc_vrange_allocator.clone_varying (type);
}

vrange_storage *ggc_alloc_vrange_storage (const vrange &r)
{
  return ggc_vrange_allocator.clone (r);
}
