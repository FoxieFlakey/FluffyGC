#ifndef _headers_1686390737_FluffyGC_FluffyHeap
#define _headers_1686390737_FluffyGC_FluffyHeap

#include <sys/types.h>
#include <stddef.h>
#include <time.h>

#ifndef __FLUFFYHEAP_EXPORT
# ifdef __GNUC__
#   define __FLUFFYHEAP_EXPORT __attribute__((visibility("default"))) extern
# else
#   define __FLUFFYHEAP_EXPORT extern
# endif
#endif

#if __clang__
# define __FLUFFYHEAP_NONNULL(t) t _Nonnull
# define __FLUFFYHEAP_NULLABLE(t) t _Nullable
#elif __GNUC__
# define __FLUFFYHEAP_NONNULL(t) t __attribute__((nonnull))
# define __FLUFFYHEAP_NULLABLE(t) t
#else
# define __FLUFFYHEAP_NONNULL(t) f
# define __FLUFFYHEAP_NULLABLE(t) f
#endif

// Types
typedef struct fh_200d21fd_f92b_41e0_9ea3_32eaf2b4e455 fluffyheap;
typedef struct fh_2530dc70_b8d3_41d5_808a_922504f90ff6 fh_context;
typedef struct fh_e96bee3a_e673_4a27_97e3_a365dc3d9d53 fh_object;
typedef struct fh_7f49677c_68b4_40ea_96c7_db3f6176c5dc fh_array;
typedef struct fh_d5564c56_0195_411c_bb8e_6293c07ec0d3 fh_type_info;
typedef struct fh_b737dbfb_246b_4486_ba7e_25c7f8e8d41d fh_ref_array_info;
typedef struct fh_fc200f7a_174f_4882_9fa4_1205af13686e fh_descriptor;
typedef struct fh_f39dbb2f_d8d1_4687_8486_a196de7712a3 fh_descriptor_param;
typedef struct fh_89fe10c0_cf25_435b_b5a3_96e5e1e5ac98 fh_param;
typedef struct fh_783de216_08ed_4ad5_ade6_8da1c47e6cb0 fh_descriptor_field;
typedef   enum fh_02261b19_abfa_4c90_93e2_c9887232e2ae fh_gc_hint;
typedef   enum fh_975ffc9a_3ef7_4b0c_91a7_eeb74d09a086 fh_reference_strength;
typedef   enum fh_d466bd64_51d5_4157_8f05_5d344bbcb85c fh_object_type;
typedef   enum fh_6174940f_5413_4fd0_8945_2e882c5a0102 fh_mod;

typedef int (*fh_descriptor_loader)(__FLUFFYHEAP_NONNULL(const char*) name, __FLUFFYHEAP_NULLABLE(void*) udata, __FLUFFYHEAP_NONNULL(struct fh_f39dbb2f_d8d1_4687_8486_a196de7712a3*) param);
typedef void (*fh_finalizer)(__FLUFFYHEAP_NONNULL(const void*) objData);

enum fh_02261b19_abfa_4c90_93e2_c9887232e2ae {
  FH_GC_BALANCED,
  FH_GC_LOW_LATENCY,
  FH_GC_HIGH_THROUGHPUT,
  FH_GC_COUNT
};

#define FH_HEAP_LOW_POWER 0x0001
#define FH_HEAP_LOW_LOAD  0x0002

struct fh_89fe10c0_cf25_435b_b5a3_96e5e1e5ac98 {
  fh_gc_hint hint;
  unsigned long flags;
  
  size_t generationCount;
  __FLUFFYHEAP_NONNULL(size_t*) generationSizes;
};

enum fh_d466bd64_51d5_4157_8f05_5d344bbcb85c {
  FH_TYPE_NORMAL,
  FH_TYPE_ARRAY,
  FH_TYPE_COUNT
};

enum fh_975ffc9a_3ef7_4b0c_91a7_eeb74d09a086 {
  FH_REF_STRONG,
  FH_REF_SOFT,
  FH_REF_WEAK,
  FH_REF_PHANTOM,
  FH_REF_COUNT
};

struct fh_783de216_08ed_4ad5_ade6_8da1c47e6cb0 {
  __FLUFFYHEAP_NONNULL(const char*) name;
  size_t offset;
  __FLUFFYHEAP_NONNULL(const char*) dataType;
  fh_reference_strength strength;
};

struct fh_f39dbb2f_d8d1_4687_8486_a196de7712a3 {
  size_t size;
  
  __FLUFFYHEAP_NULLABLE(fh_finalizer) finalizer;
  __FLUFFYHEAP_NULLABLE(fh_descriptor_field*) fields;
};

#define FH_FIELD(type, member, _dataType, refStrength) \
  { \
    .name = #member, \
    .offset = offsetof(type, member), \
    .dataType = (_dataType), \
    .strength = (refStrength) \
  }
#define FH_FIELD_END() {.name = NULL}
#define FH_CAST_TO_OBJECT(obj) _Generic((obj), \
  fh_array*: (fh_object*) (obj) \
)
#define FH_CAST_TO_ARRAY(obj) _Generic((obj), \
  fh_object*: (fh_array*) (obj) \
)

struct fh_d5564c56_0195_411c_bb8e_6293c07ec0d3 {
  fh_object_type type;
  union {
    __FLUFFYHEAP_NONNULL(fh_descriptor*) normal;
    __FLUFFYHEAP_NONNULL(fh_ref_array_info*) refArray;
  } info;
};

struct fh_b737dbfb_246b_4486_ba7e_25c7f8e8d41d {
  size_t length;
  __FLUFFYHEAP_NONNULL(fh_descriptor*) elementDescriptor;
};

// Heap stuff
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fluffyheap*) fh_new(__FLUFFYHEAP_NONNULL(fh_param*) param);
__FLUFFYHEAP_EXPORT void fh_free(__FLUFFYHEAP_NONNULL(fluffyheap*) self);

__FLUFFYHEAP_EXPORT int fh_attach_thread(__FLUFFYHEAP_NONNULL(fluffyheap*) self);
__FLUFFYHEAP_EXPORT void fh_detach_thread(__FLUFFYHEAP_NONNULL(fluffyheap*) self);

__FLUFFYHEAP_EXPORT int fh_get_generation_count(__FLUFFYHEAP_NONNULL(fh_param*) param);

__FLUFFYHEAP_EXPORT void fh_set_descriptor_loader(__FLUFFYHEAP_NONNULL(fluffyheap*) self, __FLUFFYHEAP_NULLABLE(fh_descriptor_loader) loader);
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_descriptor_loader) fh_get_descriptor_loader(__FLUFFYHEAP_NONNULL(fluffyheap*) self);

// Mods stuff
enum fh_6174940f_5413_4fd0_8945_2e882c5a0102 {
  FH_MOD_UNKNOWN  = 0x0000,
  FH_MOD_DMA      = 0x0001,
  FH_MOD_DEBUG    = 0x0002,
  FH_MOD_COUNT
};

#define FH_MOD_WAS_ENABLED (1L << 31)

__FLUFFYHEAP_EXPORT int fh_enable_mod(fh_mod mod, unsigned long flags);
__FLUFFYHEAP_EXPORT void fh_disable_mod(fh_mod mod);
__FLUFFYHEAP_EXPORT bool fh_check_mod(fh_mod mod, unsigned long flags);
__FLUFFYHEAP_EXPORT unsigned long fh_get_flags(fh_mod mod);

// Context stuff
__FLUFFYHEAP_EXPORT int fh_set_current(__FLUFFYHEAP_NONNULL(fh_context*) context);
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_context*) fh_get_current();

__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_context*) fh_new_context(__FLUFFYHEAP_NONNULL(fluffyheap*) heap);
__FLUFFYHEAP_EXPORT void fh_free_context(__FLUFFYHEAP_NONNULL(fluffyheap*) heap, __FLUFFYHEAP_NONNULL(fh_context*) self);

__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NONNULL(fluffyheap*) fh_context_get_heap(__FLUFFYHEAP_NONNULL(fh_context*) self);

// Root stuff
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_object*) fh_dup_ref(__FLUFFYHEAP_NULLABLE(fh_object*) ref);
__FLUFFYHEAP_EXPORT void fh_del_ref(__FLUFFYHEAP_NONNULL(fh_object*) ref);

// Objects stuff
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_object*) fh_alloc_object(__FLUFFYHEAP_NONNULL(fh_descriptor*) desc);

__FLUFFYHEAP_EXPORT void fh_object_read_data(__FLUFFYHEAP_NONNULL(fh_object*) self, __FLUFFYHEAP_NONNULL(void*) buffer, size_t offset, size_t size);
__FLUFFYHEAP_EXPORT void fh_object_write_data(__FLUFFYHEAP_NONNULL(fh_object*) self, __FLUFFYHEAP_NONNULL(const void*) buffer, size_t offset, size_t size);
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_object*) fh_object_read_ref(__FLUFFYHEAP_NONNULL(fh_object*) self, size_t offset);
__FLUFFYHEAP_EXPORT void fh_object_write_ref(__FLUFFYHEAP_NONNULL(fh_object*) self, size_t offset, __FLUFFYHEAP_NULLABLE(fh_object*) data);

__FLUFFYHEAP_EXPORT int fh_object_init_synchronization_structs(__FLUFFYHEAP_NONNULL(fh_object*) self);
__FLUFFYHEAP_EXPORT void fh_object_wait(__FLUFFYHEAP_NONNULL(fh_object*) self, __FLUFFYHEAP_NULLABLE(const struct timespec*) timeout);
__FLUFFYHEAP_EXPORT void fh_object_wake(__FLUFFYHEAP_NONNULL(fh_object*) self);
__FLUFFYHEAP_EXPORT void fh_object_wake_all(__FLUFFYHEAP_NONNULL(fh_object*) self);
__FLUFFYHEAP_EXPORT void fh_object_lock(__FLUFFYHEAP_NONNULL(fh_object*) self);
__FLUFFYHEAP_EXPORT void fh_object_unlock(__FLUFFYHEAP_NONNULL(fh_object*) self);

__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NONNULL(const fh_type_info*) fh_object_get_type_info(__FLUFFYHEAP_NONNULL(fh_object*) self);
__FLUFFYHEAP_EXPORT void fh_object_put_type_info(__FLUFFYHEAP_NONNULL(fh_object*) self, __FLUFFYHEAP_NONNULL(const fh_type_info*) typeInfo);

__FLUFFYHEAP_EXPORT bool fh_object_is_alias(__FLUFFYHEAP_NULLABLE(fh_object*) a, __FLUFFYHEAP_NULLABLE(fh_object*) b);
__FLUFFYHEAP_EXPORT fh_object_type fh_object_get_type(__FLUFFYHEAP_NONNULL(fh_object*) self);

// Arrays stuff
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_array*) fh_alloc_array(__FLUFFYHEAP_NONNULL(fh_descriptor*) elementType, size_t length);

__FLUFFYHEAP_EXPORT ssize_t fh_array_calc_offset(__FLUFFYHEAP_NONNULL(fh_array*) self, size_t index);
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_object*) fh_array_get_element(__FLUFFYHEAP_NONNULL(fh_array*) self, size_t index);
__FLUFFYHEAP_EXPORT void fh_array_set_element(__FLUFFYHEAP_NONNULL(fh_array*) self, size_t index, __FLUFFYHEAP_NULLABLE(fh_object*) object);
__FLUFFYHEAP_EXPORT ssize_t fh_array_get_length(__FLUFFYHEAP_NONNULL(fh_array*) self);

// Descriptors stuff
__FLUFFYHEAP_EXPORT int fh_define_descriptor(__FLUFFYHEAP_NONNULL(__FLUFFYHEAP_NULLABLE(fh_descriptor*)*) resultDescriptor, __FLUFFYHEAP_NONNULL(const char*) name, __FLUFFYHEAP_NONNULL(fh_descriptor_param*) parameter, bool dontInvokeLoader);
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NULLABLE(fh_descriptor*) fh_get_descriptor(__FLUFFYHEAP_NONNULL(const char*) name, bool dontInvokeLoader);
__FLUFFYHEAP_EXPORT void fh_release_descriptor(__FLUFFYHEAP_NULLABLE(fh_descriptor*) desc);

// This call invalid for arrays
// Used for reflective access the GC referecnes in it (e.g.
// to analyze heap content for leaks)
__FLUFFYHEAP_EXPORT __FLUFFYHEAP_NONNULL(const fh_descriptor_param*) fh_descriptor_get_param(__FLUFFYHEAP_NONNULL(fh_descriptor*) self);

#endif

