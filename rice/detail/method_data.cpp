#include "method_data.hpp"
#include "ruby.hpp"

// TODO: This is silly, autoconf...
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include "../config.hpp"

#ifndef RUBY_VM
/* pre-YARV */
#include <node.h>
#include "env.hpp"
#endif

namespace
{

VALUE DATA_MAGIC = INT2NUM(0xDA7A);

} // namespace

#ifdef RUBY_VM

VALUE
Rice::detail::
method_data()
{
  ID id;
  VALUE origin;
  if (!rb_frame_method_id_and_class(&id, &origin))
  {
    rb_raise(
        rb_eRuntimeError,
        "Cannot get method id and class for function");
  }

  VALUE memo = rb_ivar_get(origin, 0);

  if(rb_type(memo) != T_ARRAY && RARRAY_PTR(memo)[0] != DATA_MAGIC)
  {
    /* This can happen for module functions that are created after
     * the stub function */
    rb_raise(
        rb_eRuntimeError,
        "Cannot find method data for module function");
  }

  return RARRAY_PTR(memo)[1];
}

#else

/* pre-YARV */

VALUE
Rice::detail::
method_data()
{
  VALUE origin = ruby_frame->last_class;
  VALUE memo = rb_ivar_get(origin, 0);
  return RARRAY_PTR(memo)[1];
}

#endif

// Define a method and attach data to it.
// The method looks to ruby like a normal aliased CFUNC, with a modified
// origin class.
//
// How this works:
//
// To store method data and have it registered with the GC, we need a
// "slot" to put it in.  This "slot" must be recognized and marked by
// the garbage collector.  There happens to be such a place we can put
// data, and it has to do with aliased methods.  When Ruby creates an
// alias for a method, it stores a reference to the original class in
// the method entry.  The form of the method entry differs from ruby
// version to ruby version, but the concept is the same across all of
// them.
// 
// In Rice, we make use of this by defining a method on a dummy class,
// then attaching that method to our real class.  The method is a real
// method in our class, but its origin class is our dummy class.
// 
// When Ruby makes a method call, it stores the origin class in the
// current stack frame.  When Ruby calls into Rice, we grab the origin
// class from the stack frame, then pull the data out of the origin
// class.  The data item is then used to determine how to convert
// arguments and return type, how to handle exceptions, etc.
//
// It used to be the case that Rice would "fix" the call frame so that
// the modified origin class was not visible to the called function (it
// would appear to the callee that the origin class was the same as the
// class it was defined on).  However, this required modifying the call
// frame directly, and the layout of that frame varies from version to
// version.  To keep things simple (and as a side effect improve
// performance), Rice no longer hides the modified origin class this way.
//
// Functions that make use of "last_class" (1.8) or
// "rb_frame_method_id_and_class" (1.9) will therefore not get the
// results they expect.
VALUE
Rice::detail::
define_method_with_data(
    VALUE klass, ID id, VALUE (*cfunc)(ANYARGS), int arity, VALUE data)
{
  /* TODO: origin should have #to_s and #inspect methods defined */
#ifdef HAVE_RB_CLASS_BOOT
  VALUE origin = rb_class_boot(klass);
#else
  VALUE origin = rb_class_new(klass);
#endif
  VALUE memo = rb_assoc_new(DATA_MAGIC, data);
  VALUE real_class_name = rb_class_name(klass);
  VALUE origin_class_name = rb_str_plus(
      real_class_name,
      rb_str_new2("<data wrapper>"));

  FL_SET(origin, FL_SINGLETON);
  rb_singleton_class_attached(origin, klass);
  rb_name_class(origin, SYM2ID(rb_str_intern(origin_class_name)));
  rb_ivar_set(origin, 0, memo);

  /* YARV */
  st_data_t dummy_entry;
  rb_define_method(
      origin,
      rb_id2name(id),
      cfunc,
      arity);
  rb_alias(
      origin,
      rb_intern("dummy"),
      id);
  st_lookup(RCLASS_M_TBL(origin), rb_intern("dummy"), &dummy_entry);
  st_insert(RCLASS_M_TBL(klass), id, dummy_entry);

  // Clear the table so we don't try to double-free the method entry
  RCLASS_M_TBL(origin) = st_init_numtable();

  return Qnil;
}

