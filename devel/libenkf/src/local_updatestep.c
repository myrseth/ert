#include <util.h>
#include <hash.h>
#include <vector.h>
#include <local_ministep.h>
#include <local_updatestep.h>
#include <enkf_macros.h>

/**
   One enkf update is described/configured by the data structure in
   local_ministep.c. This file implements a local report_step, which
   is a collection of ministeps - in many cases a local_updatestep will
   only consist of one single local_ministep; but in principle it can
   contain several.
*/

#define LOCAL_UPDATESTEP_TYPE_ID 77159

struct local_updatestep_struct {
  int           __type_id;
  char        * name;
  vector_type * ministep;
};



SAFE_CAST(local_updatestep , LOCAL_UPDATESTEP_TYPE_ID)


local_updatestep_type * local_updatestep_alloc( const char * name ) {
  local_updatestep_type * updatestep = util_malloc( sizeof * updatestep , __func__);

  updatestep->name      = util_alloc_string_copy( name );
  updatestep->ministep  = vector_alloc_new();
  updatestep->__type_id = LOCAL_UPDATESTEP_TYPE_ID;
  return updatestep;
}


/**
   Observe that use_count values are not copied. 
*/
local_updatestep_type * local_updatestep_alloc_copy( const local_updatestep_type * src , const char * name ) {
  local_updatestep_type * new = local_updatestep_alloc( name );
  for (int i = 0; i < vector_get_size(src->ministep ); i++)
    local_updatestep_add_ministep( new , vector_iget( src->ministep , i) );
  return new;
}


void local_updatestep_free( local_updatestep_type * updatestep) {
  free( updatestep->name );
  vector_free( updatestep->ministep );
  free( updatestep );
}


void local_updatestep_free__(void * arg) {
  local_updatestep_type * updatestep = local_updatestep_safe_cast( arg );
  local_updatestep_free( updatestep );
}


void local_updatestep_add_ministep( local_updatestep_type * updatestep , local_ministep_type * ministep) {
  vector_append_ref( updatestep->ministep , ministep );   /* Observe that the vector takes NO ownership */
}



local_ministep_type * local_updatestep_iget_ministep( const local_updatestep_type * updatestep , int index) {
  return vector_iget( updatestep->ministep , index );
}


int local_updatestep_get_num_ministep( const local_updatestep_type * updatestep) {
  return vector_get_size( updatestep->ministep );
}
