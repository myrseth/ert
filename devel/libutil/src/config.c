#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <util.h>
#include <config.h>
#include <hash.h>
#include <stringlist.h>
#include <set.h>


#define CLEAR_STRING "__RESET__"



/**
   Structure to parse configuration files of this type:

KEYWORD1  ARG2   ARG2  ARG3
KEYWORD2  ARG1-2
....
KEYWORDN  



Validating
----------
The config object implements three different ways of validating the input:

 1. If the xx__argc_minmax() function has been called, a line will not
    be accepted if the number of arguments is not within this range.

 2. If the type_map has been installed for the item (with the
    xx_arg_minmax function), it is checked the arguments of the item
    are in accordance with this typemap.

 3. If the item is added with required_set == true, the validate
    routine will fail if the item is not set.

Observe that the two first steps are checked when the item is parsed
(however the error is not reported before after the parsing is
complete),  whereas the last is checked when the parsing is
complete. Observe that is ABSOLUTELY ESSENTIAL that the final call
to config_parse() is with validate == true, otherwise the validation 
will not be performed / acted upon.
*/


/**

                                                              
                           =============================                                
                           | config_type object        |
                           |                           |                                
                           | Contains 'all' the        |                                
                           | configuration information.|                                
                           |                           |                                
                           =============================                                
                               |                   |
                               |                   \________________________                                             
                               |                                            \          
                              KEY1                                         KEY2   
                               |                                             |  
                              \|/                                           \|/ 
                   =========================                      =========================          
                   | config_item object    |                      | config_item object    |                     
                   |                       |                      |                       |                     
                   | Indexed by a keyword  |                      | Indexed by a keyword  |                     
                   | which is the first    |                      | which is the first    |                     
                   | string in the         |                      | string in the         |                     
                   | config file.          |                      | config file.          |                     
                   |                       |                      |                       |                                                                           
                   =========================                      =========================                     
                       |             |                                        |
                       |             |                                        |
                      \|/           \|/                                      \|/  
============================  ============================   ============================
| config_item_node object  |  | config_item_node object  |   | config_item_node object  |
|                          |  |                          |   |                          |
| Only containing the      |  | Only containing the      |   | Only containing the      |
| stringlist object        |  | stringlist object        |   | stringlist object        |
| directly parsed from the |  | directly parsed from the |   | directly parsed from the |
| file.                    |  | file.                    |   | file.                    |
|--------------------------|  |--------------------------|   |--------------------------|
| ARG1 ARG2 ARG3           |  | VERBOSE                  |   | DEBUG                    |
============================  ============================   ============================


The example illustrated above would correspond to the following config
file (invariant under line-permutations):

KEY1   ARG1 ARG2 ARG3
KEY1   VERBOSE
KEY2   DEBUG


Example config file(2):

OUTFILE   filename
INPUT     filename
OPTIONS   store
OPTIONS   verbose
OPTIONS   optimize cache=1

In this case the whole config object will contain three items,
corresponding to the keywords OUTFILE, INPUT and OPTIONS. The two
first will again only contain one node each, whereas the OPTIONS item
will contain three nodes, corresponding to the three times the keyword
"OPTIONS" appear in the config file. Observe that *IF* the OPTIONS
item had been added with append_arg == false, only the last occurence,
corresponding to 'optimize cache=1' would be present.

*/



struct config_struct {
  hash_type            * items;                     /* A hash of config_items - the actual content. */
  stringlist_type      * parse_errors;              /* A stringlist containg the errors found when parsing.*/
  set_type             * parsed_files;              /* A set of config files whcih have been parsed - to protect against circular includes. */
};



#define CONFIG_ITEM_ID 6751
struct config_item_struct {
  int                         __id;                            /* Used for run-time checking */
  char                        * kw;                            /* The kw which identifies this item� */

  int                           alloc_size;              /* The number of nodes which have been allocated. */  
  int                           node_size;               /* The number of active nodes.*/
  config_item_node_type      ** nodes;                   /* A vector of config_item_node_type instances. */

  bool                          append_arg;              /* Should the values be appended if a keyword appears several times in the config file. */
  bool                          currently_set;           /* Has a value been assigned to this keyword. */
  bool                          required_set;            
  stringlist_type             * selection_set;           /* A list of strings which the value(s) must match (can be NULL) */
  stringlist_type             * required_children;       /* A list of item's which must also be set (if this item is set). (can be NULL) */
  hash_type                   * required_children_value; /* A list of item's which must also be set - depending on the value of this item. (can be NULL) (This one is complex). */
  int                           argc_min;                /* The minimum number of arguments for this keyword -1 means no lower limit. */
  int                           argc_max;                /* The maximum number of arguments for this keyword (on one line) -1 means no limit. */   
  config_item_types           * type_map;                /* A list of types for the items - can be NULL. Set along with argc_minmax(); */
};


struct config_item_node_struct {
  stringlist_type             * stringlist;              /* The values which have been set. */
};

/*****************************************************************/


static void config_item_node_fprintf(config_item_node_type * node , int node_nr , FILE * stream) {
  fprintf(stream , "   %02d: ",node_nr);
  stringlist_fprintf(node->stringlist , " " , stream);
  fprintf(stream , "\n");
}


static config_item_node_type * config_item_node_alloc() {
  config_item_node_type * node = util_malloc(sizeof * node , __func__);
  node->stringlist = stringlist_alloc_new();
  return node;
}


static void config_item_node_append(config_item_node_type * node , const char * arg) {
  stringlist_append_copy( node->stringlist , arg );
}


static void config_item_node_free(config_item_node_type * node) {
  stringlist_free(node->stringlist);
  free(node);
}


static char * config_item_node_validate( const config_item_node_type * node , const config_item_types * type_map) {
  int i;
  char * error_message = NULL;
  for (i = 0; i < stringlist_get_size( node->stringlist ); i++) {
    const char * value = stringlist_iget(node->stringlist , i);
    switch (type_map[i]) {
    case(CONFIG_STRING): /* This never fails ... */
      break;
    case(CONFIG_EXECUTABLE):
      {
	char * executable = util_alloc_PATH_executable( value );
	if (executable == NULL) 
	  error_message = util_alloc_sprintf("Could not locate executable:%s ", value);
	else
	  free(executable);
      }
      break;
    case(CONFIG_INT):
      if (!util_sscanf_int( value , NULL ))
        error_message = util_alloc_sprintf("Failed to parse:%s as an integer.",value);
      break;
    case(CONFIG_FLOAT):
      if (!util_sscanf_double( value , NULL ))
        error_message = util_alloc_sprintf("Failed to parse:%s as a floating point number.", value);
      break;
    case(CONFIG_EXISTING_FILE):
      if (!util_file_exists(value))
        error_message = util_alloc_sprintf("Can not find file: %s. ",value);
      break;
    case(CONFIG_EXISTING_DIR):
      if (!util_is_directory(value))
        error_message = util_alloc_sprintf("Can not find directory: %s. ",value);
      break;
    case(CONFIG_BOOLEAN):
      if (!util_sscanf_bool( value , NULL ))
        error_message = util_alloc_sprintf("Failed to parse:%s as a boolean.", value);
      break;
    case(CONFIG_BYTESIZE):
      if (!util_sscanf_bytesize( value , NULL))
        error_message = util_alloc_sprintf("Failed to parse:\"%s\" as number of bytes." , value);
      break;
    default:
      util_abort("%s: config_item_type:%d not recognized \n",__func__ , type_map[i]);
    }
  }
  return error_message;
}




static void config_item_realloc_nodes(config_item_type * item , int new_size) {
  const int old_size = item->alloc_size;
  item->nodes      = util_realloc(item->nodes , sizeof * item->nodes * new_size , __func__);
  item->alloc_size = new_size;
  {
    int i;
    for (i=old_size; i < new_size; i++)
      item->nodes[i] = NULL;
  }
}


static void config_item_node_clear(config_item_node_type * node) {
  stringlist_clear( node->stringlist );
}


static config_item_node_type * config_item_iget_node(const config_item_type * item , int index) {
  if (index < 0 || index >= item->node_size)
    util_abort("%s: error - asked for node nr:%d available: [0,%d> \n",__func__ , index , item->node_size);
  return item->nodes[index];
}


/** 
    Adds a new node as side-effect ... 
*/
static config_item_node_type * config_item_get_new_node(config_item_type * item) {
  if (item->node_size == item->alloc_size)
    config_item_realloc_nodes(item , item->alloc_size * 2);
  {
    config_item_node_type * new_node = config_item_node_alloc();
    item->nodes[item->node_size] = new_node;
    item->node_size++;
    return new_node;
  }
}


static config_item_node_type * config_item_get_first_node(config_item_type * item) {

  if (item->node_size == 0)
    config_item_get_new_node(item); 
  
  return config_item_iget_node(item , 0);
}


/*
  This function will fail item has not been allocated 
  append_arg == false.
*/

const char * config_item_iget(const config_item_type * item , int index) {
  if (item->append_arg) 
    util_abort("%s: kw:%s this function can only be used on items added with append_arg == FALSE\n" , __func__ , item->kw);
  {
    config_item_node_type * node = config_item_iget_node(item , 0);  
    return stringlist_iget( node->stringlist , index );
  }
}

/*
  This function will fail if we can not satisfy argc_minmax = (1,1).
*/
const char * config_item_get(const config_item_type * item) {
  if ((item->argc_min == 1) && (item->argc_max == 1))
    return config_item_iget(item , 0);
  else {
    util_abort("%s: kw:%s sorry - this function requires that argc_minmax = 1,1 \n",__func__ , item->kw);
    return NULL;
  }
}


static const stringlist_type * config_item_get_stringlist_ref(const config_item_type * item) {
  if (item->append_arg) 
    util_abort("%s: this function can only be used on items added with append_arg == FALSE\n" , __func__);
  {
    config_item_node_type * node = config_item_iget_node(item , 0);  
    return node->stringlist;
  }
}


/**
   If copy == false - the stringlist will break down when/if the
   config object is freed - your call.
*/
   
static stringlist_type * config_item_alloc_complete_stringlist(const config_item_type * item, bool copy) {
  int inode;
  stringlist_type * stringlist = stringlist_alloc_new();
  for (inode = 0; inode < item->node_size; inode++) {

    if (copy)
      stringlist_insert_stringlist_copy( stringlist , item->nodes[inode]->stringlist );
    else
      stringlist_insert_stringlist_ref( stringlist , item->nodes[inode]->stringlist );  
    
  }

  return stringlist;
}


/**
   If copy == false - the hash will break down when/if the
   config object is freed - your call.
*/

static hash_type * config_item_alloc_hash(const config_item_type * item , bool copy) {
  hash_type * hash = hash_alloc();
  int inode;
  for (inode = 0; inode < item->node_size; inode++) {
    const config_item_node_type * node = item->nodes[inode];

    if (copy)
      hash_insert_hash_owned_ref(hash , stringlist_iget(node->stringlist , 0) , util_alloc_string_copy(stringlist_iget(node->stringlist , 1)) , free);
    else
      hash_insert_ref(hash , stringlist_iget(node->stringlist , 0) , stringlist_iget(node->stringlist , 1));
    
  }
  return hash;
}


config_item_type * config_item_alloc(const char * kw , bool required , bool append_arg) {
  config_item_type * item = util_malloc(sizeof * item , __func__);

  item->__id       = CONFIG_ITEM_ID;
  item->kw         = util_alloc_string_copy(kw);
  item->alloc_size = 0;
  item->node_size  = 0;
  item->nodes      = NULL;
  config_item_realloc_nodes(item , 1);

  item->currently_set           = false;
  item->append_arg              = append_arg;
  item->required_set            = required;
  item->argc_min          = -1;  /* -1 - not applicable */
  item->argc_max          = -1;
  item->selection_set           = NULL;
  item->required_children       = NULL;
  item->required_children_value = NULL;
  item->type_map                = NULL;
  return item;
}



/* 
   Observe that this function is a bit funny - because it will
   actually free the incoming message.
*/

static void config_add_and_free_error(config_type * config , char * error_message) {
  if (error_message != NULL) {
    int error_nr = stringlist_get_size(config->parse_errors) + 1;
    stringlist_append_owned_ref(config->parse_errors , util_alloc_sprintf("  %02d: %s" , error_nr , error_message));
    free(error_message);
  }
}

/**
   Used to reset an item is the special string 'CLEAR_STRING'
   is found as the only argument:

   OPTION V1
   OPTION V2 V3 V4
   OPTION __RESET__ 
   OPTION V6

   In this case OPTION will get the value 'V6'. The example given
   above is a bit contrived; this option is designed for situations
   where several config files are parsed serially; and the user can
   not/will not update the first.
*/

static void config_item_clear( config_item_type * item ) {
  int i;
  for (i = 0; i < item->node_size; i++)
    config_item_node_free( item->nodes[i] );
  item->nodes = util_safe_free(item->nodes);
  item->node_size     = 0;
  item->currently_set = false;
  config_item_realloc_nodes(item , 1);
}


/*
  The last argument (config_file) is only used for printing
  informative error messages, and can be NULL.

  Returns a string with an error description, or NULL if the supplied
  arguments were OK. The string is allocated here, but is assumed that
  calling scope will free it.
*/

char * config_item_set_arg(config_item_type * item , int argc , const char ** argv , const char * config_file) {
  if (argc == 1 && (strcmp(argv[0] , CLEAR_STRING) == 0)) {
    config_item_clear(item);
    return NULL;
  } else {
    char * error_message = NULL;
    int iarg;
    bool OK            = true;
    bool currently_set = false;
    config_item_node_type * node;
    
    if (item->append_arg)
      node = config_item_get_new_node(item);
    else {
      node = config_item_get_first_node(item);
      config_item_node_clear(node);
    }
    
    
    if (item->argc_min >= 0) {
      if (argc < item->argc_min) {
        OK = false;
        
        if (config_file != NULL)
          error_message = util_alloc_sprintf("Error when parsing config_file:\"%s\" Keyword:%s must have at least %d arguments.",config_file , item->kw , item->argc_min);
        else
          error_message = util_alloc_sprintf("Error:: Keyword:%s must have at least %d arguments.",item->kw , item->argc_min);
      }
    }
    
    if (item->argc_max >= 0) {
      if (argc > item->argc_max) {
        OK = false;
        if (config_file != NULL)
          error_message = util_alloc_sprintf("Error when parsing config_file:\"%s\" Keyword:%s must have maximum %d arguments.",config_file , item->kw , item->argc_min);
        else
          error_message = util_alloc_sprintf("Error:: Keyword:%s must have maximum %d arguments.",item->kw , item->argc_min);
      }
    }
    
    if (OK) {
      if (argc == 0) /* It is OK to set without arguments */
        currently_set = true;
      else {
        for (iarg = 0; iarg < argc; iarg++) {
          OK = true;
          if (item->selection_set != NULL) {
            if (!stringlist_contains(item->selection_set , argv[iarg])) {
              error_message = util_alloc_sprintf("%s: is not a valid value for: %s.",argv[iarg] , item->kw);
              OK = false;
            } 
          }
          if (OK) {
            config_item_node_append(node , argv[iarg]);
            currently_set = true;
          }
        }
      }
    }
    if (currently_set)
      item->currently_set = true;
    return error_message;
  }
}




static int config_item_get_occurences(const config_item_type * item) {
  return item->node_size;
}


void config_item_validate(config_type * config , const config_item_type * item) {
  
  if (item->currently_set) {
    if (item->type_map != NULL) {
      int inode;
      for (inode = 0; inode < item->node_size; inode++) {
        char * error_message = config_item_node_validate(item->nodes[inode] , item->type_map);
        if (error_message != NULL) {
          config_add_and_free_error(config , error_message);
        }
      }
    }

    if (item->required_children != NULL) {
      int i;
      for (i = 0; i < stringlist_get_size(item->required_children); i++) {
        if (!config_has_set_item(config , stringlist_iget(item->required_children , i))) {
          char * error_message = util_alloc_sprintf("When:%s is set - you also must set:%s.",item->kw , stringlist_iget(item->required_children , i));
          config_add_and_free_error(config , error_message);
        }
      }
    }

    if (item->required_children_value != NULL) {
      int inode;
      for (inode = 0; inode < config_item_get_occurences(item); inode++) {
        config_item_node_type * node   = config_item_iget_node(item , inode);
        stringlist_type       * values = node->stringlist;
        int is;

        for (is = 0; is < stringlist_get_size(values); is++) {
          const char * value = stringlist_iget(values , is);
          if (hash_has_key(item->required_children_value , value)) {
            stringlist_type * children = hash_get(item->required_children_value , value);
            int ic;
            for (ic = 0; ic < stringlist_get_size( children ); ic++) {
              const char * req_child = stringlist_iget( children , ic );
              if (!config_has_set_item(config , req_child )) {
                char * error_message = util_alloc_sprintf("When:%s is set to:%s - you also must set:%s.",item->kw , value , req_child );
                config_add_and_free_error(config , error_message);
              }
            }
          }
        }
      }
    }
  } else if (item->required_set) {  /* The item is not set ... */
    char * error_message = util_alloc_sprintf("Item:%s must be set.",item->kw);
    config_add_and_free_error(config , error_message);
  }
}


void config_item_fprintf(const config_item_type * item , FILE * stream) {
  fprintf(stream, "%s \n",item->kw);
  for (int i=0; i < item->node_size; i++) 
    config_item_node_fprintf(item->nodes[i] , i , stream);

  if (item->required_children_value != NULL) {
    char ** values = hash_alloc_keylist( item->required_children_value );
    
    for (int i=0; i < hash_get_size(item->required_children_value ); i++) {
      fprintf(stream , "  %-10s: ",values[i]);
      stringlist_fprintf(hash_get( item->required_children_value , values[i]) , " " , stream);
      fprintf(stream , "\n");
    }
    
    util_free_stringlist( values , hash_get_size( item->required_children_value ));
  }
}


void config_item_free( config_item_type * item) {
  /*
    printf("%s / %s: \n",__func__ , item->kw);
    config_item_fprintf(item , stdout);
  */
  free(item->kw);
  {
    int i;
    for (i = 0; i < item->node_size; i++)
      config_item_node_free( item->nodes[i] );
    free(item->nodes);
  }
  if (item->required_children       != NULL) stringlist_free(item->required_children);
  if (item->required_children_value != NULL) hash_free(item->required_children_value); 
  if (item->selection_set != NULL)           stringlist_free(item->selection_set);
  util_safe_free(item->type_map);
  free(item);
}





void config_item_free__ (void * void_item) {
  config_item_type * item = (config_item_type *) void_item;
  if (item->__id != CONFIG_ITEM_ID) 
    util_abort("%s: internal error - cast failed \n",__func__);
  
  config_item_free( item );
}


bool config_item_is_set(const config_item_type * item) {
  return item->currently_set;
}

void config_item_set_selection_set(config_item_type * item , const stringlist_type * stringlist) {
  if (item->selection_set != NULL)
    stringlist_free(item->selection_set);
  
  item->selection_set = stringlist_alloc_deep_copy(stringlist);
}

void config_item_add_to_selection(config_item_type * item , const char *value) {
  if (item->selection_set == NULL)
    item->selection_set = stringlist_alloc_new();
  stringlist_append_copy(item->selection_set , value);
}

static bool config_item_has_selection_item(config_item_type * item, const char * value) {
  bool has_item = false;
  if (item->selection_set != NULL) {
    if (stringlist_contains(item->selection_set , value))
      has_item = true;
  }
  return has_item;
}

void config_item_set_required_children(config_item_type * item , stringlist_type * stringlist) {
  item->required_children = stringlist_alloc_deep_copy(stringlist);
}



/**
   This works in the following way: 

     if item == value {
        All children in child_list must also be set.
     }

     
*/        


void config_item_set_required_children_on_value(config_item_type * item , const char * value , stringlist_type * child_list) {
  if (config_item_has_selection_item(item , value)) {
    if (item->required_children_value == NULL)
      item->required_children_value = hash_alloc();
    hash_insert_hash_owned_ref( item->required_children_value , value , stringlist_alloc_deep_copy(child_list) , stringlist_free__);
  } else 
    util_abort("%s: must install selection set which includes:%s first \n",__func__ , value);
}


/**
   This function is used to set the minimum and maximum number of
   arguments for an item. In addition you can pass in a pointer to an
   array of config_item_types values which will be used for validation
   of the input. This vector must be argc_max elements long; it can be
   NULL.
*/


void config_item_set_argc_minmax(config_item_type * item , int argc_min , int argc_max, const config_item_types * type_map) {
  item->argc_min = argc_min;
  item->argc_max = argc_max;

  util_safe_free(item->type_map);
  if (type_map != NULL)
    item->type_map = util_alloc_copy(type_map , argc_max * sizeof * type_map , __func__);
  
}
  


#undef __TYPE__



/*****************************************************************/



config_type * config_alloc() {
  config_type *config     = util_malloc(sizeof * config  , __func__);
  config->items           = hash_alloc();
  config->parse_errors    = stringlist_alloc_new();
  config->parsed_files    = set_alloc_empty();
  return config;
}


void config_free(config_type * config) {
  hash_free(config->items);
  stringlist_free(config->parse_errors);
  set_free(config->parsed_files);
  free(config);
}




/**
   This function allocates a simple item with all values
   defaulted. The item is added to the config object, and a pointer is
   returned to the calling scope. If you want to change the properties
   of the item you can do that with config_item_set_xxxx() functions
   from the calling scope.
*/


config_item_type * config_add_item(config_type * config , 
                                   const char  * kw, 
                                   bool  required  , 
                                   bool  append_arg) {
  
  config_item_type * item = config_item_alloc( kw , required , append_arg);
  hash_insert_hash_owned_ref(config->items , kw , item , config_item_free__);
  
  return item;
}







bool config_has_item(const config_type * config , const char * kw) {
  return hash_has_key(config->items , kw);
}

config_item_type * config_get_item(const config_type * config , const char * kw) {
  return hash_get(config->items , kw);
}

bool config_item_set(const config_type * config , const char * kw) {
  return config_item_is_set(hash_get(config->items , kw));
}

void config_set_arg(config_type * config , const char * kw, int argc , const char **argv) {
  char * error_message = config_item_set_arg(config_get_item(config , kw) , argc , argv , NULL);
  config_add_and_free_error(config , error_message);
}








char ** config_alloc_active_list(const config_type * config, int * _active_size) {
  char ** complete_key_list = hash_alloc_keylist(config->items);
  char ** active_key_list = NULL;
  int complete_size = hash_get_size(config->items);
  int active_size   = 0;
  int i;

  for( i = 0; i < complete_size; i++) {
    if  (config_item_is_set(config_get_item(config , complete_key_list[i]) )) {
      active_key_list = util_stringlist_append_copy(active_key_list , active_size , complete_key_list[i]);
      active_size++;
    }
  }
  *_active_size = active_size;
  util_free_stringlist(complete_key_list , complete_size);
  return active_key_list;
}




static void config_validate(config_type * config, const char * filename) {
  int size = hash_get_size(config->items);
  char ** key_list = hash_alloc_keylist(config->items);
  int ikey;
  for (ikey = 0; ikey < size; ikey++) {
    const config_item_type * item = config_get_item(config , key_list[ikey]);
    config_item_validate(config , item);
  }
  util_free_stringlist(key_list , size);
  if (stringlist_get_size(config->parse_errors) > 0) {
    fprintf(stderr,"Parsing errors:\n");
    stringlist_fprintf(config->parse_errors , "\n", stderr);
    util_exit("");
  }
}



/**
   This function parses the config file 'filename', and updated the
   internal state of the config object as parsing proceeds. If
   comment_string != NULL everything following 'comment_string' on a
   line is discarded.

   include_kw is a string identifier for an include functionality, if
   an include is encountered, the included file is parsed immediately
   (through a recursive call to config_parse). if include_kw == NULL,
   include files are not supported.

   auto_add: whether unrecognized keywords should be added to the the
             config object.  

   validate: whether we should validate when complete, that should
             typically only be done at the last parsing.

*/


void config_parse(config_type * config , const char * filename, const char * comment_string , const char * include_kw ,bool auto_add , bool validate) {
  char * abs_filename = util_alloc_realpath(filename);

  if (!set_add_key(config->parsed_files , abs_filename)) 
    util_exit("%s: file:%s already parsed - circular include ? \n",__func__ , filename);
  else {
    FILE * stream = util_fopen(filename , "r");
    bool   at_eof = false;
  
    while (!at_eof) {
      int i , tokens;
      int active_tokens;
      char **token_list;
      char  *line;
    
      line  = util_fscanf_alloc_line(stream , &at_eof);
      if (line != NULL) {
	util_split_string(line , " \t" , &tokens , &token_list);
      
        active_tokens = tokens;
        for (i = 0; i < tokens; i++) {
          char * comment_ptr = NULL;
          if(comment_string != NULL)
            comment_ptr = strstr(token_list[i] , comment_string);
          
          if (comment_ptr != NULL) {
            if (comment_ptr == token_list[i])
              active_tokens = i;
            else
              active_tokens = i + 1;
            break;
          }
        }

        if (active_tokens > 0) {
          const char * kw = token_list[0];
	  if (include_kw != NULL && (strcmp(include_kw , kw) == 0)) {
	    if (active_tokens != 2) 
	      util_abort("%s: keyword:%s must have exactly one argument. \n",__func__ ,include_kw);
	    config_parse(config , token_list[1] , comment_string , include_kw , auto_add , false); /* Recursive call */
	  } else {
	    if (!config_has_item(config , kw) && auto_add) 
	      config_add_item(config , kw , true , false);  /* Auto created items get append_arg == false, and required == true (which is trivially satisfied). */
	    
	    if (config_has_item(config , kw)) {
	      config_item_type * item = config_get_item(config , kw);
	      config_item_set_arg(item , active_tokens - 1, (const char **) &token_list[1] , filename);
	    } else 
	      fprintf(stderr,"** Warning keyword:%s not recognized when parsing:%s - ignored \n",kw,filename);
	  }
        }
        util_free_stringlist(token_list , tokens);
        free(line);
      }
    }
    if (validate) config_validate(config , filename);
    fclose(stream);
  }
  free(abs_filename);
}



bool config_has_keys(const config_type * config, const char **ext_keys, int ext_num_keys, bool exactly)
{
  int i;

  int     config_num_keys;
  char ** config_keys;

  config_keys = config_alloc_active_list(config, &config_num_keys);

  if(exactly && (config_num_keys != ext_num_keys))
  {
    util_free_stringlist(config_keys,config_num_keys);
    return false;
  }

  for(i=0; i<ext_num_keys; i++)
  {
    if(!config_has_item(config,ext_keys[i]))
    {
      util_free_stringlist(config_keys,config_num_keys);
      return false;
    }
  }
 
  util_free_stringlist(config_keys,config_num_keys);
  return true;
}




/*****************************************************************/
/* Here comes some xxx_get() functions - many of them will fail if
   the item has not been added in the right way (this is to ensure that
   the xxx_get() request is unambigous. */


/**
   This function can be used to get the value of a config
   parameter. But to ensure that the get is unambigous we set the
   following requirements to the item corresponding to 'kw':

    * It has been added with append_arg == false.
    * argc_minmax has been set to 1,1

   If this is not the case - we die.
*/

const char * config_get(const config_type * config , const char * kw) {
  config_item_type * item = config_get_item(config , kw);

  return config_item_get(item);
}



/** 
    As the config_get function, but the argc_minmax requiremnt has been removed.
*/
const char * config_iget(const config_type * config , const char * kw, int index) {
  config_item_type * item = config_get_item(config , kw);

  return config_item_iget(item , index);
}



/**
   This returns A REFERENCE to the stringlist of an item, assuming the
   item corresponding to 'kw':

    * It has been added with append_arg == false.

   If this is not the case - we die.
*/



const stringlist_type * config_get_stringlist_ref(const config_type * config , const char * kw) {
  config_item_type * item = config_get_item(config , kw);

  return config_item_get_stringlist_ref(item);
}


/**
  This function allocates a new stringlist containing *ALL* the
  arguements for an item. With reference to the illustrated example at
  the top the function call:

     config_alloc_complete_strtinglist(config , "KEY1");

     would produce the list: ("ARG1" "ARG2" "ARG2" "VERBOSE"), i.e. the
  arguments for the various occurences of "KEY1" are collapsed to one
  stringlist.
*/

  
stringlist_type * config_alloc_complete_stringlist(const config_type* config , const char * kw) {
  bool copy = true;
  config_item_type * item = config_get_item(config , kw);
  return config_item_alloc_complete_stringlist(item , copy);
}


/**
   Return the number of times a keyword has been set - dies on unknown 'kw';
*/

int config_get_occurences(const config_type * config, const char * kw) {
  return config_item_get_occurences(config_get_item(config , kw));
}



/**
   Allocates a hash table for situations like this:

ENV   PATH              /some/path
ENV   LD_LIBARRY_PATH   /some/other/path
ENV   MALLOC            STRICT
....

the returned hash table will be: {"PATH": "/som/path", "LD_LIBARRY_PATH": "/some/other_path" , "MALLOC": "STRICT"}

It is enforced that:

 * item is allocated with append_arg = true
 * item is allocated with argc_minmax = 2,2
 
 The hash takes copy of the values in the hash so the config object
 can safefly be freed (opposite if copy == false).
*/


hash_type * config_alloc_hash(const config_type * config , const char * kw) {
  bool copy = true;
  config_item_type * item = config_get_item(config , kw);
  return config_item_alloc_hash(item , copy);
}



bool config_has_set_item(const config_type * config , const char * kw) {
  if (config_has_item(config , kw)) {
    config_item_type * item = config_get_item(config , kw);
    return config_item_is_set(item);
  } else
    return false;
}







