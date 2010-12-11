/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "interpreter/bytecode.hpp"
#include "interpreter/linkResolver.hpp"
#include "oops/constantPoolOop.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/fieldType.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/signature.hpp"

// Implementation of Bytecode

bool Bytecode::check_must_rewrite(Bytecodes::Code code) const {
  assert(Bytecodes::can_rewrite(code), "post-check only");

  // Some codes are conditionally rewriting.  Look closely at them.
  switch (code) {
  case Bytecodes::_aload_0:
    // Even if RewriteFrequentPairs is turned on,
    // the _aload_0 code might delay its rewrite until
    // a following _getfield rewrites itself.
    return false;

  case Bytecodes::_lookupswitch:
    return false;  // the rewrite is not done by the interpreter

  case Bytecodes::_new:
    // (Could actually look at the class here, but the profit would be small.)
    return false;  // the rewrite is not always done
  }

  // No other special cases.
  return true;
}


#ifdef ASSERT

void Bytecode::assert_same_format_as(Bytecodes::Code testbc, bool is_wide) const {
  Bytecodes::Code thisbc = Bytecodes::cast(byte_at(0));
  if (thisbc == Bytecodes::_breakpoint)  return;  // let the assertion fail silently
  if (is_wide) {
    assert(thisbc == Bytecodes::_wide, "expected a wide instruction");
    thisbc = Bytecodes::cast(byte_at(1));
    if (thisbc == Bytecodes::_breakpoint)  return;
  }
  int thisflags = Bytecodes::flags(testbc, is_wide) & Bytecodes::_all_fmt_bits;
  int testflags = Bytecodes::flags(thisbc, is_wide) & Bytecodes::_all_fmt_bits;
  if (thisflags != testflags)
    tty->print_cr("assert_same_format_as(%d) failed on bc=%d%s; %d != %d",
                  (int)testbc, (int)thisbc, (is_wide?"/wide":""), testflags, thisflags);
  assert(thisflags == testflags, "expected format");
}

void Bytecode::assert_index_size(int size, Bytecodes::Code bc, bool is_wide) {
  int have_fmt = (Bytecodes::flags(bc, is_wide)
                  & (Bytecodes::_fmt_has_u2 | Bytecodes::_fmt_has_u4 |
                     Bytecodes::_fmt_not_simple |
                     // Not an offset field:
                     Bytecodes::_fmt_has_o));
  int need_fmt = -1;
  switch (size) {
  case 1: need_fmt = 0;                      break;
  case 2: need_fmt = Bytecodes::_fmt_has_u2; break;
  case 4: need_fmt = Bytecodes::_fmt_has_u4; break;
  }
  if (is_wide)  need_fmt |= Bytecodes::_fmt_not_simple;
  if (have_fmt != need_fmt) {
    tty->print_cr("assert_index_size %d: bc=%d%s %d != %d", size, bc, (is_wide?"/wide":""), have_fmt, need_fmt);
    assert(have_fmt == need_fmt, "assert_index_size");
  }
}

void Bytecode::assert_offset_size(int size, Bytecodes::Code bc, bool is_wide) {
  int have_fmt = Bytecodes::flags(bc, is_wide) & Bytecodes::_all_fmt_bits;
  int need_fmt = -1;
  switch (size) {
  case 2: need_fmt = Bytecodes::_fmt_bo2; break;
  case 4: need_fmt = Bytecodes::_fmt_bo4; break;
  }
  if (is_wide)  need_fmt |= Bytecodes::_fmt_not_simple;
  if (have_fmt != need_fmt) {
    tty->print_cr("assert_offset_size %d: bc=%d%s %d != %d", size, bc, (is_wide?"/wide":""), have_fmt, need_fmt);
    assert(have_fmt == need_fmt, "assert_offset_size");
  }
}

void Bytecode::assert_constant_size(int size, int where, Bytecodes::Code bc, bool is_wide) {
  int have_fmt = Bytecodes::flags(bc, is_wide) & (Bytecodes::_all_fmt_bits
                                                  // Ignore any 'i' field (for iinc):
                                                  & ~Bytecodes::_fmt_has_i);
  int need_fmt = -1;
  switch (size) {
  case 1: need_fmt = Bytecodes::_fmt_bc;                          break;
  case 2: need_fmt = Bytecodes::_fmt_bc | Bytecodes::_fmt_has_u2; break;
  }
  if (is_wide)  need_fmt |= Bytecodes::_fmt_not_simple;
  int length = is_wide ? Bytecodes::wide_length_for(bc) : Bytecodes::length_for(bc);
  if (have_fmt != need_fmt || where + size != length) {
    tty->print_cr("assert_constant_size %d @%d: bc=%d%s %d != %d", size, where, bc, (is_wide?"/wide":""), have_fmt, need_fmt);
  }
  assert(have_fmt == need_fmt, "assert_constant_size");
  assert(where + size == length, "assert_constant_size oob");
}

void Bytecode::assert_native_index(Bytecodes::Code bc, bool is_wide) {
  assert((Bytecodes::flags(bc, is_wide) & Bytecodes::_fmt_has_nbo) != 0, "native index");
}

#endif //ASSERT

// Implementation of Bytecode_tableupswitch

int Bytecode_tableswitch::dest_offset_at(int i) const {
  return get_Java_u4_at(aligned_offset(1 + (3 + i)*jintSize));
}


// Implementation of Bytecode_invoke

void Bytecode_invoke::verify() const {
  assert(is_valid(), "check invoke");
  assert(method()->constants()->cache() != NULL, "do not call this from verifier or rewriter");
}


symbolOop Bytecode_member_ref::signature() const {
  constantPoolOop constants = method()->constants();
  return constants->signature_ref_at(index());
}


symbolOop Bytecode_member_ref::name() const {
  constantPoolOop constants = method()->constants();
  return constants->name_ref_at(index());
}


BasicType Bytecode_member_ref::result_type(Thread *thread) const {
  symbolHandle sh(thread, signature());
  ResultTypeFinder rts(sh);
  rts.iterate();
  return rts.type();
}


methodHandle Bytecode_invoke::static_target(TRAPS) {
  methodHandle m;
  KlassHandle resolved_klass;
  constantPoolHandle constants(THREAD, _method->constants());

  if (java_code() == Bytecodes::_invokedynamic) {
    LinkResolver::resolve_dynamic_method(m, resolved_klass, constants, index(), CHECK_(methodHandle()));
  } else if (java_code() != Bytecodes::_invokeinterface) {
    LinkResolver::resolve_method(m, resolved_klass, constants, index(), CHECK_(methodHandle()));
  } else {
    LinkResolver::resolve_interface_method(m, resolved_klass, constants, index(), CHECK_(methodHandle()));
  }
  return m;
}


int Bytecode_member_ref::index() const {
  // Note:  Rewriter::rewrite changes the Java_u2 of an invokedynamic to a native_u4,
  // at the same time it allocates per-call-site CP cache entries.
  Bytecodes::Code rawc = code();
  Bytecode* invoke = bytecode();
  if (invoke->has_index_u4(rawc))
    return invoke->get_index_u4(rawc);
  else
    return invoke->get_index_u2_cpcache(rawc);
}

int Bytecode_member_ref::pool_index() const {
  int index = this->index();
  DEBUG_ONLY({
      if (!bytecode()->has_index_u4(code()))
        index -= constantPoolOopDesc::CPCACHE_INDEX_TAG;
    });
  return _method->constants()->cache()->entry_at(index)->constant_pool_index();
}

// Implementation of Bytecode_field

void Bytecode_field::verify() const {
  assert(is_valid(), "check field");
}


// Implementation of Bytecode_loadconstant

int Bytecode_loadconstant::raw_index() const {
  Bytecode* bcp = bytecode();
  Bytecodes::Code rawc = bcp->code();
  assert(rawc != Bytecodes::_wide, "verifier prevents this");
  if (Bytecodes::java_code(rawc) == Bytecodes::_ldc)
    return bcp->get_index_u1(rawc);
  else
    return bcp->get_index_u2(rawc, false);
}

int Bytecode_loadconstant::pool_index() const {
  int index = raw_index();
  if (has_cache_index()) {
    return _method->constants()->cache()->entry_at(index)->constant_pool_index();
  }
  return index;
}

BasicType Bytecode_loadconstant::result_type() const {
  int index = pool_index();
  constantTag tag = _method->constants()->tag_at(index);
  return tag.basic_type();
}

oop Bytecode_loadconstant::resolve_constant(TRAPS) const {
  assert(_method.not_null(), "must supply method to resolve constant");
  int index = raw_index();
  constantPoolOop constants = _method->constants();
  if (has_cache_index()) {
    return constants->resolve_cached_constant_at(index, THREAD);
  } else {
    return constants->resolve_constant_at(index, THREAD);
  }
}

//------------------------------------------------------------------------------
// Non-product code

#ifndef PRODUCT

void Bytecode_lookupswitch::verify() const {
  switch (Bytecodes::java_code(code())) {
    case Bytecodes::_lookupswitch:
      { int i = number_of_pairs() - 1;
        while (i-- > 0) {
          assert(pair_at(i)->match() < pair_at(i+1)->match(), "unsorted table entries");
        }
      }
      break;
    default:
      fatal("not a lookupswitch bytecode");
  }
}

void Bytecode_tableswitch::verify() const {
  switch (Bytecodes::java_code(code())) {
    case Bytecodes::_tableswitch:
      { int lo = low_key();
        int hi = high_key();
        assert (hi >= lo, "incorrect hi/lo values in tableswitch");
        int i  = hi - lo - 1 ;
        while (i-- > 0) {
          // no special check needed
        }
      }
      break;
    default:
      fatal("not a tableswitch bytecode");
  }
}

#endif
