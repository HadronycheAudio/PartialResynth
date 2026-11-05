/* Just enough of Pure Data's API for fiddle~.c to compile and run outside Pd, in the tests. */

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef float t_float;
typedef float t_floatarg;
typedef intptr_t t_int;

typedef void (*t_method) (void);
typedef void* (*t_newmethod) (void);

typedef struct _pd { int unused; } t_pd;
typedef struct _object { t_pd ob_pd; } t_object;
typedef struct _symbol { int unused; } t_symbol;
typedef struct _clock { int unused; } t_clock;
typedef struct _class { size_t size; } t_class;
typedef struct _atom { t_float value; } t_atom;
typedef struct _signal { t_float* s_vec; int s_n; t_float s_sr; } t_signal;

enum { A_FLOAT = 1, A_DEFFLOAT = 2 };

#define SETFLOAT(atom, f) ((atom)->value = (t_float) (f))

static t_class referenceClass;

static void* getbytes (size_t size) { return calloc (size ? size : 1, 1); }
static void freebytes (void* bytes, size_t size) { (void) size; free (bytes); }
static void post (const char* format, ...) { (void) format; }
static void pd_error (const void* object, const char* format, ...) { (void) object; (void) format; }
static t_symbol* gensym (const char* name) { (void) name; return NULL; }
static void* pd_new (t_class* c) { return calloc (c->size, 1); }
static void pd_free (t_pd* object) { free (object); }
static void* outlet_new (t_object* object, t_symbol* type) { (void) object; (void) type; return NULL; }
static void outlet_float (void* outlet, t_float value) { (void) outlet; (void) value; }
static void outlet_bang (void* outlet) { (void) outlet; }
static void outlet_list (void* outlet, t_symbol* type, int count, t_atom* atoms) { (void) outlet; (void) type; (void) count; (void) atoms; }
static t_clock* clock_new (void* owner, t_method method) { (void) owner; (void) method; return NULL; }
static void clock_delay (t_clock* clock, double delay) { (void) clock; (void) delay; }
static void clock_free (t_clock* clock) { (void) clock; }
static void dsp_add (t_int* (*perform) (t_int*), int count, ...) { (void) perform; (void) count; }
static void nullfn (void) {}
static t_class* class_new (t_symbol* name, t_newmethod create, t_method destroy, size_t size, int flags, int type, ...)
{
    (void) name; (void) create; (void) destroy; (void) flags; (void) type;
    referenceClass.size = size;
    return &referenceClass;
}
static void class_addmethod (t_class* c, t_method method, t_symbol* name, int type, ...) { (void) c; (void) method; (void) name; (void) type; }
static void class_addcreator (t_newmethod create, t_symbol* name, int type, ...) { (void) create; (void) name; (void) type; }
#define class_addbang(c, method) ((void) (c), (void) (method))

/* Pd's FFT: forward when inverse is 0, in place, unscaled, on interleaved complex points. */
void pd_fft (t_float* buffer, int npoints, int inverse);
