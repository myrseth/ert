#include <util.h>
#include <hash.h>
#include <stdio.h>
#include <string.h>
#include <ecl_file.h>
#include <ecl_util.h>
#include <vector.h>
#include <ecl_grid.h>
#include <math.h>

#define WATER 1
#define GAS   2
#define OIL   4


typedef struct {
  double utm_x; 
  double utm_y; 
  double depth; 
  double grav_diff;
  char * name;
} grav_station_type;



/*****************************************************************/





static void truncate_saturation(float * value) {
  util_apply_float_limits( value , 0.0 , 1.0);
}



static bool has_phase( int phase_sum , int phase) {
  if ((phase_sum & phase) == 0)
    return false;
  else
    return true;
}


static const float * safe_get_float_ptr( const ecl_kw_type * ecl_kw , const float * alternative) {
  if (ecl_kw != NULL)
    return ecl_kw_get_float_ptr(ecl_kw);
  else
    return alternative;
}




/*****************************************************************/

void print_usage(int line) {
  printf("LINE: %d \n",line);
  fprintf(stderr,"This program is used to calculate the change in graviational response\n");
  fprintf(stderr,"between two timesteps in an eclipse simulation. To do the calculations\n");
  fprintf(stderr,"the program needs the following information:\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"  1. Restart file(s) with solution data for the two timesteps.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"  2. An EGRID or GRID file.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"  3. An INIT file.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"  4. A configuration file which lists at which geographical locations\n");
  fprintf(stderr,"     you want to measure the gravitational response. This file should\n");
  fprintf(stderr,"     contain one position on each line, formatted as this:\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"             name1   utm_x  utm_y   depth\n");
  fprintf(stderr,"             name2   utm_x  utm_y   depth\n");
  fprintf(stderr,"             .....\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"     The name string is completely arbitrary - but can NOT contain\n");
  fprintf(stderr,"     spaces.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"The required information should be passed from the user with the help\n");
  fprintf(stderr,"of commandline arguments. This can be done in roughly speaking two\n");
  fprintf(stderr,"different ways:\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"All ECLIPSE files in one directory\n");
  fprintf(stderr,"----------------------------------\n");
  fprintf(stderr,"In the case where all the files are found in one directory you can\n");
  fprintf(stderr,"just give an ECLIPSE basename, and the run_gravity program will by\n");
  fprintf(stderr,"itself find the required restart/init/grid files. Observe that both\n");
  fprintf(stderr,"unified and non-unified restart files will be checked. In addition to\n");
  fprintf(stderr,"the ECLIPSE basename you must give two numbers indicating which report\n");
  fprintf(stderr,"steps you are interested in comparing, and finally the configuration\n");
  fprintf(stderr,"file with all the measurement positions.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"Example:\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"    bash%%  run_gravity.x  BASE_CASE  10 178  ../config/grav_stations\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"This will look up restart/grid/init files in the current dirtectory,\n");
  fprintf(stderr,"for a simulation with baseame 'BASE_CASE'. It will compare report\n");
  fprintf(stderr,"steps 10 and 178, and load station locations from the file\n");
  fprintf(stderr,"'../config/grav_stations'. \n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"ECLIPSE files NOT in same directory\n");
  fprintf(stderr,"-----------------------------------\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"If the different ECLIPSE files are not in the same directory you can\n");
  fprintf(stderr,"not let the run_gravity program find the required files automatically,\n");
  fprintf(stderr,"and you must give all the required files as arguments on the command\n");
  fprintf(stderr,"line. This is the most flexible approach, in addition to files stored\n");
  fprintf(stderr,"different places this also allows to combine files with different\n");
  fprintf(stderr,"ECLISPE basenames. There are two different ways to enter restart\n");
  fprintf(stderr,"information, depending on whether you use unified or non-unified\n");
  fprintf(stderr,"restart files.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"Example 1 (unified restart):\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"     bash%% run_gravity.x /path/to/restart_files/CASE_3.UNRST 10 178  /path/init/BASE_CASE.INIT   /path/to/grid/BASE_CASE.EGRID  ../config/stations.txt\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"Example 2 (non-unified restart):\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"     bash%% run_gravity.x CASE_3.X0010  ../path/CASE_2.X0178  /path/init/BASE_CASE.INIT   /path/to/grid/BASE_CASE.EGRID  ../config/stations.txt\n");
  fprintf(stderr,"     \n");
  fprintf(stderr,"\n");
  fprintf(stderr,"  When the program has completed succesfully it will write the changes\n");
  fprintf(stderr,"  in local gravity to a file 'RUN_GRAVITY.out', in addition the same\n");
  fprintf(stderr,"  information (with something more) will be sent to stdout.\n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n");
  exit(1);
}



static void grav_station_free__( void * arg) {
  grav_station_type * grav = ( grav_station_type * ) arg;
  free( grav->name );
  free( grav );
}



static grav_station_type * grav_station_alloc(const char * name , double x, double y, double d){
  grav_station_type * s = util_malloc(sizeof*s, __func__);
  s->name  = util_alloc_string_copy( name );
  s->utm_x = x;
  s->utm_y = y;
  s->depth = d;
  s->grav_diff = 0.0;
  return s;
}




/**
   The station information is in a file with the following rules:

    1. Each station on a seperate line.
    2. For each station we have four items:

         name   utm_x  utm_y   depth
   
       name is an arbitrary string - without spaces.
*/  

static void load_stations(vector_type * grav_stations , const char * filename) {
  printf("Loading from file:%s \n",filename);
  {
    FILE * stream = util_fopen(filename , "r");
    bool at_eof = false;
    while(!(at_eof)) {
      double x,y,d;
      char station_name[32];
      int fscanf_return = fscanf(stream, "%s%lg%lg%lg", station_name , &x,&y,&d);
      if(fscanf_return == 4){
        grav_station_type * g = grav_station_alloc(station_name , x,y,d);
        vector_append_owned_ref(grav_stations, g, grav_station_free__);
      } else 
        at_eof = true;
    }
    fclose(stream);
  }
}
    





/**
   This function will load, and return two ecl_file_type instances with the
   restart information from the two relevant times. The input to this
   function is a (char **) pointer, taken directly from the argv input
   pointer.
   
   The function will start by calling ecl_util_get_file_type(input[0]), and
   depending on the return value from this call it will follow three
   different code-paths:


     ECL_FILE_OTHER: This means that the first argument should be
        interpreted not as an existing file name, but rather as an ECLIPSE
        base name. The program will look for restart info in files in the
        working directory with the following order:

	 1. Unified restart file - unformatted.
	 2. Non unified restart files - unformatted.
	 3. Unified restart file - formatted.
	 4. Non unified restart files - formatted.
	
	The search will stop at the first success, if no restart
	information is found the function will exit. The remaining
	arguments in input[] will not be considered, but observe that the
	use of ecl_base is signalled back to calling scope (through
	reference), and the calling scope will look for GRID file and INIT
	file also based on the ECLBASE found input[0]; formatted /
	unformatted will be as returned from the four-way switch above.
 
	Example:
	
	bash% run_gravity  ECLIPSE   10  128   xxxx
	


     ECL_RESTART_FILE: This means that input[0] is a non unified eclipse
        restart file, this file will be loaded. And it is ASSUMED that
        input[1] is the next non - unified restart file, loaded for the
        next report step.

	Example:

	bash% run_gravity ECLIPSE.X0010  ECLIPSE.X0128   xxx
	

     
     ECL_UNIFIED_RESTART_FILE: This means that input[1] and input[2] are
        interpreted as integers (i.e. report steps), and those two report
        steps will be loaded from the unified restart file pointed to by
        input[0].

	Example:

	bash% run_gravity ECLIPSE.UNRST  10 128 xxx

 

    Observe that in all the examples above 'xxx' signifies argv arguments
    which this function does not care about. The return the *arg_offset
    variable will be set to indicate this index:

    char ** input = argv[1];
    int     input_offset;
    ecl_restart_file_type ** restart_info = load_restart_info(input , &input_offset, ...);
    
    Then the next argument is: input[input_offset];
*/


static ecl_file_type ** load_restart_info(const char ** input,           /* Input taken directly from argv */
					  int           input_length,    /* The length of input. */
					  int         * arg_offset,      /* Integer - value corresponding to the *NEXT* element in input which should be used by the calling scope. */
					  bool        * use_eclbase,     /* Should input[0] be interpreted as an ECLBASE string? */
					  bool        * fmt_file) {      /* Only relevant if (*use_eclbase == true): was formatted file used? */
  
  
  ecl_file_type ** restart_files = util_malloc( 2 * sizeof * restart_files , __func__);
  int  report_nr;
  ecl_file_enum file_type;

  *use_eclbase = false;
  ecl_util_get_file_type( input[0] , &file_type , fmt_file , &report_nr );
  
  if (file_type == ECL_RESTART_FILE) {
    /* Loading from two non-unified restart files. */
    if (input_length >= 2) {
      ecl_util_get_file_type( input[1] , &file_type , fmt_file , &report_nr );
      if (file_type == ECL_RESTART_FILE) {
	restart_files[0] = ecl_file_fread_alloc( input[0] );
	restart_files[1] = ecl_file_fread_alloc( input[1] );
	*arg_offset = 2;
      } else print_usage(__LINE__);
    } else print_usage(__LINE__);
  } else if (file_type == ECL_UNIFIED_RESTART_FILE) {
    /* Loading from one unified restart file. */
    if (input_length >= 3) {
      int report1 , report2;
      if ((util_sscanf_int( input[1] , &report1) && util_sscanf_int( input[2] , &report2))) {
	restart_files[0] = ecl_file_fread_alloc_unrst_section( input[0] , report1 );
	restart_files[1] = ecl_file_fread_alloc_unrst_section( input[0] , report2 );
	*arg_offset = 3;
      } else
	print_usage(__LINE__);
    } else 
      print_usage(__LINE__);
  } else if (file_type == ECL_OTHER_FILE) {
    if (input_length >= 3) {
      int report1, report2;
      if (!(util_sscanf_int( input[1] , &report1) && util_sscanf_int( input[2] , &report2)))
	print_usage(__LINE__);
      else {
	/* 
	   input[0] is interpreted as an eclbase string, and not as the name of
	   an existing file. Go through various combinations of
	   unified/non-unified formatted/unformatted to find data.
	*/
	ecl_storage_enum storage_mode = ECL_INVALID_STORAGE;
	const char * eclbase = input[0];
	char * unified_file  = NULL;
	char * file1	     = NULL;
	char * file2	     = NULL;       
	
	unified_file = ecl_util_alloc_filename(NULL , eclbase , ECL_UNIFIED_RESTART_FILE , false , -1);
	if (util_file_exists( unified_file )) 
	  /* Binary unified */
	  storage_mode = ECL_BINARY_UNIFIED;
	else {
	  /* Binary non-unified */
	  file1 = ecl_util_alloc_filename(NULL , eclbase , ECL_RESTART_FILE , false , report1);
	  file2 = ecl_util_alloc_filename(NULL , eclbase , ECL_RESTART_FILE , false , report2);
	  if ((util_file_exists(file1) && util_file_exists(file2))) 
	    storage_mode = ECL_BINARY_NON_UNIFIED;
	  else {
	    free(unified_file);
	    /* ASCII unified */
	    unified_file = ecl_util_alloc_filename(NULL , eclbase , ECL_UNIFIED_RESTART_FILE , true , -1);
	    if (util_file_exists( unified_file ))
	      storage_mode = ECL_FORMATTED_UNIFIED;
	    else {
	      /* ASCII non unified */
	      free(file1);
	      free(file2);
	      file1 = ecl_util_alloc_filename(NULL , eclbase , ECL_RESTART_FILE , true , report1);
	      file2 = ecl_util_alloc_filename(NULL , eclbase , ECL_RESTART_FILE , true , report2);
	      if ((util_file_exists(file1) && util_file_exists(file2))) 
		storage_mode = ECL_FORMATTED_UNIFIED;
	    }
	  }
	}
	
	if (storage_mode == ECL_INVALID_STORAGE) {
	  char * cwd = util_alloc_cwd();
	  util_exit("Could not find any restart information for ECLBASE:%s in %s \n", eclbase , cwd);
	  free( cwd );
	}

	if ((storage_mode == ECL_BINARY_UNIFIED) || (storage_mode == ECL_FORMATTED_UNIFIED)) {
	  restart_files[0] = ecl_file_fread_alloc_unrst_section( unified_file , report1 );
	  restart_files[1] = ecl_file_fread_alloc_unrst_section( unified_file , report2 );
	} else {
	  restart_files[0] = ecl_file_fread_alloc( file1 );
	  restart_files[1] = ecl_file_fread_alloc( file2 );
	}
	  


	*use_eclbase = true;
	if ((storage_mode == ECL_BINARY_UNIFIED) || (storage_mode == ECL_BINARY_NON_UNIFIED))
	  *fmt_file = false;
	else
	  *fmt_file = true;
	
	*arg_offset = 3;
	
	util_safe_free( file1 );
	util_safe_free( file2 );
	util_safe_free( unified_file );
      }
    }
  }
  return restart_files;
}



/*
  This function calculates the gravimetric response (local change in
  g) at location (utm_x , utm_y , depth) - i.e. the function is
  written as stand-alone, and is independent of the (somewhat
  arbitrary) datatype grav_station_type.

  For code cleanliness the code is written in a way where this
  function is called for every position we are interested in,
  performance-wise it would be smarter to loop over the interesting
  locations as the inner loop.
  
  This function does NOT check whether the restart_file / init_file
  contains the necessary keywords - and will fail HARD if a required
  keyword is not present. That the the input is well-formed should be
  checked PRIOR to calling this function.
*/

static double gravity_response(const ecl_grid_type * ecl_grid      , 
                               const ecl_file_type * init_file     , 
                               const ecl_file_type * restart_file1 , 
                               const ecl_file_type * restart_file2 ,
                               double utm_x , 
                               double utm_y , 
                               double tvd   , 
                               int model_phases, 
                               int file_phases) {
  
  ecl_kw_type * rporv1_kw   = NULL;  
  ecl_kw_type * rporv2_kw   = NULL;
  ecl_kw_type * oil_den1_kw = NULL;  
  ecl_kw_type * oil_den2_kw = NULL;
  ecl_kw_type * gas_den1_kw = NULL;
  ecl_kw_type * gas_den2_kw = NULL;
  ecl_kw_type * wat_den1_kw = NULL;
  ecl_kw_type * wat_den2_kw = NULL;
  ecl_kw_type * sgas1_kw    = NULL;
  ecl_kw_type * sgas2_kw    = NULL;
  ecl_kw_type * swat1_kw    = NULL;
  ecl_kw_type * swat2_kw    = NULL;
  ecl_kw_type * aquifern_kw = NULL ;
  double local_deltag = 0;

  /* Extracting the pore volumes */
  rporv1_kw = ecl_file_iget_named_kw( restart_file1 , "RPORV" , 0);      
  rporv2_kw = ecl_file_iget_named_kw( restart_file2 , "RPORV" , 0);      
  
  
  /** Extracting the densities */
  {
    // OIL_DEN
    if( has_phase(model_phases , OIL) ) {
      oil_den1_kw  = ecl_file_iget_named_kw(restart_file1, "OIL_DEN", 0);
      oil_den2_kw  = ecl_file_iget_named_kw(restart_file2, "OIL_DEN", 0);
    }
    
    // GAS_DEN
    if( has_phase( model_phases , GAS) ) {
      gas_den1_kw  = ecl_file_iget_named_kw(restart_file1, "GAS_DEN", 0);
      gas_den2_kw  = ecl_file_iget_named_kw(restart_file2, "GAS_DEN", 0);
    }
    
    // WAT_DEN
    if( has_phase( model_phases , WATER) ) {
      wat_den1_kw  = ecl_file_iget_named_kw(restart_file1, "WAT_DEN", 0);
      wat_den2_kw  = ecl_file_iget_named_kw(restart_file2, "WAT_DEN", 0);
    }
  }
  
  
  /* Extracting the saturations */
  {
    // SGAS
    if( has_phase( file_phases , GAS )) {
      sgas1_kw     = ecl_file_iget_named_kw(restart_file1, "SGAS", 0);
      sgas2_kw     = ecl_file_iget_named_kw(restart_file2, "SGAS", 0);
    } 
    
    // SWAT
    if( has_phase( file_phases , WATER )) {
      swat1_kw     = ecl_file_iget_named_kw(restart_file1, "SWAT", 0);
      swat2_kw     = ecl_file_iget_named_kw(restart_file2, "SWAT", 0);
    } 
  }
  
  
  /* The numerical aquifer information */
  if( ecl_file_has_kw( init_file , "AQUIFERN")) 
    aquifern_kw     = ecl_file_iget_named_kw(init_file, "AQUIFERN", 0);
  {
    int     nactive  = ecl_grid_get_active_size( ecl_grid );
    float * zero     = util_malloc( nactive * sizeof * zero     , __func__);    /* Fake vector of zeros used for densities / sturations when you do not have data. */
    int   * int_zero = util_malloc( nactive * sizeof * int_zero , __func__);    /* Fake vector of zeros used for AQUIFER when the init file does not supply data. */
    /* 
       Observe that the fake vectors are only a coding simplification,
       they should not be really used.
    */

    {
      int i;
      for (i=0; i < nactive; i++) {
        zero[i]     = 0;
        int_zero[i] = 0;
      }
    }
    {
      const float * sgas1_v   = safe_get_float_ptr( sgas1_kw    , NULL );
      const float * swat1_v   = safe_get_float_ptr( swat1_kw    , NULL );
      const float * oil_den1  = safe_get_float_ptr( oil_den1_kw , zero );
      const float * gas_den1  = safe_get_float_ptr( gas_den1_kw , zero );
      const float * wat_den1  = safe_get_float_ptr( wat_den1_kw , zero );
      
      const float * sgas2_v   = safe_get_float_ptr( sgas2_kw    , NULL );
      const float * swat2_v   = safe_get_float_ptr( swat2_kw    , NULL );
      const float * oil_den2  = safe_get_float_ptr( oil_den2_kw , zero );
      const float * gas_den2  = safe_get_float_ptr( gas_den2_kw , zero );
      const float * wat_den2  = safe_get_float_ptr( wat_den2_kw , zero );
      
      const float * rporv1    = ecl_kw_get_float_ptr(rporv1_kw);
      const float * rporv2    = ecl_kw_get_float_ptr(rporv2_kw);
      int   * aquifern;
      int global_index;
    
      if (aquifern_kw != NULL)
        aquifern = ecl_kw_get_int_ptr( aquifern_kw );
      else
        aquifern = int_zero;



      for (global_index=0;global_index < ecl_grid_get_global_size( ecl_grid ); global_index++){
        const int act_index = ecl_grid_get_active_index1( ecl_grid , global_index );
        if (act_index >= 0) {
          // Not numerical aquifer 
          if(aquifern[act_index] >= 0){ 
            float swat1 = swat1_v[act_index];
            float swat2 = swat2_v[act_index];
            float sgas1 = 0;
            float sgas2 = 0;
            float soil1 = 0;
            float soil2 = 0;

            truncate_saturation( &swat1 );
            truncate_saturation( &swat2 );
            
            if (has_phase( model_phases , GAS)) {
              if (has_phase( file_phases , GAS )) {
                sgas1 = sgas1_v[act_index];
                sgas2 = sgas2_v[act_index];
                truncate_saturation( &sgas1 );
                truncate_saturation( &sgas2 );
              } else {
                sgas1 = 1 - swat1;
                sgas2 = 1 - swat2;
              }
            }
            
            if (has_phase( model_phases , OIL )) {
              soil1 =  1 - sgas1  - swat1;
              soil2 =  1 - sgas2  - swat2;
              truncate_saturation( &soil1 );
              truncate_saturation( &soil2 );
            }
            
                        
            /* 
               We have found all the info we need for one cell.
            */
            
            {
              double  mas1 , mas2;
              double  xpos , ypos , zpos;
              
              mas1 = rporv1[act_index]*(soil1 * oil_den1[act_index] + sgas1 * gas_den1[act_index] + swat1 * wat_den1[act_index] );
              mas2 = rporv2[act_index]*(soil2 * oil_den2[act_index] + sgas2 * gas_den2[act_index] + swat2 * wat_den2[act_index] );

              ecl_grid_get_xyz1(ecl_grid , global_index , &xpos , &ypos , &zpos);
              {
                double dist_x   = xpos - utm_x;
                double dist_y   = ypos - utm_y;
                double dist_d   = zpos - tvd;
                double dist_sq  = dist_x*dist_x + dist_y*dist_y + dist_d*dist_d;
                
                if(dist_sq == 0){
                  exit(1);
                }
                local_deltag += 6.67E-3*(mas2 - mas1)*dist_d/pow(dist_sq, 1.5);
              }
              
            }
          }
        }
      }
    }
    free( zero );
    free( int_zero );
  }
  return local_deltag;
}



  
/* 
   Validate input:
   ---------------
   This function tries to verify that the restart_files contain all
   the necessary information. The required keywords are:

    1. The restart files must contain RPORV and XXX_DEN (see info
       about phases below).

    2. The init file must contain the PORV keyword - this is only used
       to check for the ECLIPSE_2008 bug in RPORV calculations.
       


   Determine phases:
   -----------------
   Look at the restart files to determine which phases are
   present. The restart files generally only contain (n - 1) phases,
   i.e. for a WATER-OIL-GAS system the restart files will contain SGAS
   and SWAT, but not SOIL.
   
   We must determine which phases are in the model, that is determined
   by looking for the densities OIL_DEN, WAT_DEN and GAS_DEN. This is
   stored in the variable model_phases. In addition we must determine
   which saturations can be found in the restart files, that is stored
   in the file_phases variable. The variables model_phases and
   file_phases are returned by reference.


   If the input is valid, the function will return zero, otherwise it
   will return a non-zero error code: (ehhh - it will exit currently).
   
*/

static int gravity_check_input( const ecl_grid_type * ecl_grid , 
                                const ecl_file_type * init_file , 
                                const ecl_file_type * restart_file1, 
                                const ecl_file_type * restart_file2,
                                int   * __model_phases,
                                int   * __file_phases) {
  {
    int model_phases = 0;
    int file_phases  = 0;
    
    /* Check which phases are present in the model */
    if (ecl_file_has_kw(restart_file1 , "OIL_DEN"))
      model_phases += OIL;			  	      
    
    if (ecl_file_has_kw(restart_file1 , "WAT_DEN"))
      model_phases += WATER;			  	      
    
    if (ecl_file_has_kw(restart_file1 , "GAS_DEN"))
      model_phases += GAS;
    
    
    /* Check which phases are present in the restart files. We assume the restart file NEVER has SOIL information */
    if (ecl_file_has_kw(restart_file1 , "SWAT"))
      file_phases += WATER;
    if (ecl_file_has_kw(restart_file1 , "SGAS"))
      file_phases += GAS;
    
    
    /* Consiency check */
    {
      /**
         The following assumptions are made:
         
         1. All restart files should have water, i.e. the SWAT keyword. 
         2. All phases present in the restart file should also be present as densities, 
            in addition the model must contain one additional phase. 
         3. The restart files can never contain oil saturation.
         
      */
      if ( !has_phase( file_phases , WATER ) )
        util_exit("Could not locate SWAT keyword in restart files\n");
      
      if ( has_phase( file_phases , OIL ))
        util_exit("Can not handle restart files with SOIL keyword\n"); 
      
      if (! has_phase( model_phases , WATER ) )
        util_exit("Could not locate WAT_DEN keyword in restart files\n");      
      
      if ( has_phase( file_phases , GAS )) {
        /** Restart file has both water and gas - means we need all three densities. */
        if (! (has_phase( model_phases , GAS) && has_phase( model_phases , OIL)))
          util_exit("Could not find GAS_DEN and OIL_DEN keywords in restart files\n");
      } else {
        /* This is (water + oil) or (water + gas) system. We enforce one of the densities.*/
        if ( !has_phase( model_phases , GAS + OIL))
          util_exit("Could not find either GAS_DEN or OIL_DEN kewyords in restart files\n");
      }
    }
    *__model_phases = model_phases;
    *__file_phases  = file_phases;
  }
  
  /* Check that the restart files have RPORV information. This is ensured by giving the argument RPORV to the RPTRST keyword. */
  if ( !(ecl_file_has_kw( restart_file1 , "RPORV") && ecl_file_has_kw( restart_file2 , "RPORV")) )
    util_exit("Sorry: the restartfiles do  not contain RPORV\n");       



  /**
     Check that the rporv values are in the right ballpark.  For
     ECLIPSE version 2008.2 they are way fucking off. Check PORV
     versus RPORV for ten 'random' locations in the grid.
  */
  {
    const ecl_kw_type * rporv1_kw     = ecl_file_iget_named_kw( restart_file1 , "RPORV" , 0);      
    const ecl_kw_type * rporv2_kw     = ecl_file_iget_named_kw( restart_file2 , "RPORV" , 0);      
    const ecl_kw_type * init_porv_kw  = ecl_file_iget_named_kw( init_file     , "PORV" , 0);

    int    active_index;
    int    active_delta;
    int    active_size;
    
    ecl_grid_get_dims( ecl_grid , NULL , NULL , NULL , &active_size );
    active_delta = active_size / 12;
    for (active_index = active_delta; active_index < active_size; active_index += active_delta) {
      int    global_index = ecl_grid_get_global_index1A( ecl_grid , active_index );
      double init_porv    = ecl_kw_iget_as_double( init_porv_kw , global_index );   /* NB - this uses global indexing. */
      double rporv1       = ecl_kw_iget_as_double( rporv1_kw ,  active_index );
      double rporv2       = ecl_kw_iget_as_double( rporv2_kw ,  active_index );
      double rporv12      = 0.5 * ( rporv1 + rporv2 );
      double fraction     = util_double_min( init_porv , rporv12 ) / util_double_max( init_porv , rporv12 );

      if (fraction  < 0.50) {
        fprintf(stderr,"-----------------------------------------------------------------\n");
        fprintf(stderr,"INIT PORV: %g \n",init_porv);
        fprintf(stderr,"RPORV1   : %g \n",rporv1);
        fprintf(stderr,"RPORV2   : %g \n",rporv2);
        fprintf(stderr,"Hmmm - the RPORV values extracted from the restart file seem to be \n");
        fprintf(stderr,"veeery different from the initial rporv value. This might indicated\n");
        fprintf(stderr,"an ECLIPSE bug. Version 2007.2 is known to be ok in this respect, \n");
        fprintf(stderr,"whereas version 2008.2 is known to have a bug. \n");
        fprintf(stderr,"-----------------------------------------------------------------\n");
        exit(1);
      }
    }
  }
  return 0;
}







/*****************************************************************/
/* Main program                                                  */
/*****************************************************************/

int main(int argc , char ** argv) {

  if(argc > 1) {
    if(strcmp(argv[1], "-h") == 0)
      print_usage(__LINE__);
  }


  if(argc < 2)
    print_usage(__LINE__);


  else{
    char ** input        = &argv[1];   /* Skipping the name of the executable */
    int     input_length = argc - 1;   
    int     input_offset = 0;
    bool    use_eclbase, fmt_file; 

    
    const char * report_filen  = "RUN_GRAVITY.out"; 
    
    ecl_file_type ** restart_files;
    ecl_file_type  * init_file;
    ecl_grid_type  * ecl_grid;
    
    int model_phases;
    int file_phases;
    vector_type * grav_stations = vector_alloc_new();
    
    
    /* Restart info */
    restart_files = load_restart_info( (const char **) input , input_length , &input_offset , &use_eclbase , &fmt_file);

    
    /* INIT and GRID/EGRID files */
    {
      char           * grid_filename = NULL;
      char           * init_filename = NULL;
      if (use_eclbase) {
	/* 
	   The first command line argument is interpreted as ECLBASE, and we
	   search for grid and init files in cwd.
	*/
	init_filename = ecl_util_alloc_exfilename_anyfmt( NULL , input[0] , ECL_INIT_FILE  , fmt_file , -1);
	grid_filename = ecl_util_alloc_exfilename_anyfmt( NULL , input[0] , ECL_EGRID_FILE , fmt_file , -1);
	if (grid_filename == NULL)
	  grid_filename = ecl_util_alloc_exfilename_anyfmt( NULL , input[0] , ECL_GRID_FILE , fmt_file , -1);
        
	if ((init_filename == NULL) || (grid_filename == NULL))  /* Means we could not find them. */
          util_exit("Could not find INIT or GRID|EGRID file \n");
      } else {
	/* */
	if ((input_length - input_offset) > 1) {
	  init_filename = util_alloc_string_copy(input[input_offset]);
	  grid_filename = util_alloc_string_copy(input[input_offset + 1]);
	  input_offset += 2;
	} else print_usage(__LINE__);
      }
      
      init_file     = ecl_file_fread_alloc(init_filename );
      ecl_grid      = ecl_grid_alloc(grid_filename );
      free( init_filename );
      free( grid_filename );
    }
    
    // Load the station_file
    if (input_length > input_offset) {
      char * station_file = input[input_offset];
      if (util_file_exists(station_file))
	load_stations( grav_stations , station_file);
      else 
        util_exit("Can not find file:%s \n",station_file);
    } else 
      print_usage(__LINE__);



    /** 
        OK - now everything is loaded - check that all required keywords+++ are present.
    */
    gravity_check_input(ecl_grid , init_file , restart_files[0] , restart_files[1] , &model_phases , &file_phases);
    
    /* 
       OK - now it seems the provided files have all the information
       we need. Let us start extracting, and then subsequently using
       it.
    */
    {
      int station_nr;
      for (station_nr = 0; station_nr < vector_get_size( grav_stations ); station_nr++) {
        grav_station_type * gs = vector_iget( grav_stations , station_nr );
        
        gs->grav_diff = gravity_response( ecl_grid , 
                                          init_file , 
                                          restart_files[0] , 
                                          restart_files[1] , 
                                          gs->utm_x,
                                          gs->utm_y,
                                          gs->depth,
                                          model_phases , 
                                          file_phases);
        
      }
    }
    
    {
      FILE * stream = util_fopen(report_filen , "w");
      int station_nr;
      for(station_nr = 0; station_nr < vector_get_size( grav_stations ); station_nr++){
	const grav_station_type * g_s = vector_iget_const(grav_stations, station_nr);
	fprintf(stream, "%f\n",g_s->grav_diff);
	printf ("DELTA_G %4s[%02d]: %12.6f %12.6f %12.6f %12.6f \n", g_s->name , station_nr, g_s->grav_diff, g_s->utm_x, g_s->utm_y, g_s->depth);
      }
      fclose(stream);
    }
    

    vector_free( grav_stations );
    ecl_grid_free(ecl_grid);
    ecl_file_free(restart_files[0]);
    ecl_file_free(restart_files[1]);
    free( restart_files );
    ecl_file_free(init_file);
  }		
}