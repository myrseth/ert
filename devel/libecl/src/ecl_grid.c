/*
   Copyright (C) 2011  Statoil ASA, Norway. 
    
   The file 'ecl_grid.c' is part of ERT - Ensemble based Reservoir Tool. 
    
   ERT is free software: you can redistribute it and/or modify 
   it under the terms of the GNU General Public License as published by 
   the Free Software Foundation, either version 3 of the License, or 
   (at your option) any later version. 
    
   ERT is distributed in the hope that it will be useful, but WITHOUT ANY 
   WARRANTY; without even the implied warranty of MERCHANTABILITY or 
   FITNESS FOR A PARTICULAR PURPOSE.   
    
   See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html> 
   for more details. 
*/

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <util.h>
#include <ecl_kw.h>
#include <ecl_grid.h>
#include <stdbool.h>
#include <ecl_util.h>
#include <double_vector.h>
#include <int_vector.h>
#include <ecl_file.h>
#include <hash.h>
#include <vector.h>
#include <stringlist.h>
#include <point.h>
#include <tetrahedron.h>
#include <thread_pool.h>

/**
  This function implements functionality to load ECLISPE grid files,
  both .EGRID and .GRID files - in a transparent fashion.

  Observe the following convention:

  global_index:  [0 , nx*ny*nz)
  active_index:  [0 , nactive)

  About indexing
  --------------

  There are three different ways to index/access a cell:

    1. By ijk
    2. By global index, [0 , nx*ny*nz)
    3. By active index, [0 , nactive)

  Most of the query functions can take input in several of the
  ways. The expected arguments are indicated as the last part of the
  function name:

    ecl_grid_get_pos3()  - 3:  this function expects i,j,k
    ecl_grid_get_pos1()  - 1:  this function expects a global index
    ecl_grid_get_pos1A() - 1A: this function expects an active index.
    
*/


/**
   Note about LGR
   --------------

   The ECLIPSE Local Grid Refinement (LGR) is organised as follows:

     1. You start with a normal grid.
     2. Some of the cells can be subdivided into further internal
        grids, this is the LGR.

   This is illustrated below:


    +--------------------------------------+
    |            |            |            |
    |            |            |            |
    |     X      |      X     |     X      |
    |            |            |            |
    |            |            |            |
    -------------|------------|------------|
    |     |      |      |     |            |
    |     |  x   |   x  |     |            |
    |-----X------|------X-----|     X      |
    |  x  |  x   |   x  |     |            |
    |     |      |      |     |            |
    -------------|------------|------------|
    |            |            |            |
    |            |            |            |
    |     X      |            |            |
    |            |            |            |
    |            |            |            |
    +--------------------------------------+


  The grid above shows the following:

    1. The coarse (i.e. normal) grid has 9 cells, of which 7 marked
       with 'X' are active.

    2. Two of the cells have been refined into new 2x2 grids. In the
       refined cells only three and two of the refined cells are
       active.

  In a GRID file the keywords for this grid will look like like this:


  .....    __
  COORDS     \
  CORNERS     |
  COORDS      |
  CORNERS     |
  COORDS      |
  CORNERS     |      Normal COORD / CORNERS kewyords
  COORDS      |      for the nine coarse cells. Observe
  CORNERS     |      that when reading in these cells it is
  COORDS      |      IMPOSSIBLE to know that some of the
  CORNERS     |      cells will be subdivieded in a following
  COORDS      |      LGR definition.
  CORNERS     |
  COORDS      |
  CORNERS     |
  COORDS      |    
  CORNERS     |      
  COORDS      |      
  CORNERS  __/________________________________________________________  
  LGR        \
  LGRILG      |     
  DIMENS      |       
  COORDS      |
  CORNERS     |      First LGR, with some header information, 
  COORDS      |      and then normal COORDS/CORNERS keywords for
  CORNERS     |      the four refined cells.
  COORDS      |
  CORNERS     |
  COORDS      |
  CORNERS  __/
  LGR        \
  LGRILG      |
  DIMENS      |
  COORDS      |      Second LGR.
  CORNERS     |
  COORDS      |
  CORNERS     |
  COORDS      |
  CORNERS     |
  COORDS      |
  CORNERS  __/

  
  For EGRID files it is essentially the same, except for replacing the
  keywords COORDS/CORNERS with COORD/ZCORN/ACTNUM. Also the LGR
  headers are somewhat different.  

  Solution data in restart files comes in a similar way, a restart
  file with LGR can typically look like this:

  .....   __
  .....     \ 
  STARTSOL   |   All restart data for the ordinary
  PRESSURE   |   grid.
  SWAT       |
  SGAS       |
  ....       |
  ENDSOL  __/____________________________ 
  LGR       \
  ....       |
  STARTSOL   |   Restart data for 
  PRESSURE   |   the first LGR.
  SGAS       |
  SWAT       |
  ...        |
  ENDSOL     |
  ENDLGR  __/   
  LGR       \ 
  ....       |
  STARTSOL   |   Restart data for 
  PRESSURE   |   the second LGR.
  SGAS       |
  SWAT       |
  ...        |
  ENDSOL     |
  ENDLGR  __/


  The LGR implementation in is based on the following main principles:

   1. When loading a EGRID/GRID file one ecl_grid_type instance will
      be allocated; this grid will contain the main grid, and all the
      lgr grids. 

   2. Only one datatype (ecl_grid_type) is used both for the main grid
      and the lgr grids.

   3. The main grid will own (memory wise) all the lgr grids, this
      even applies to nested subgrids whose parent is also a lgr.

   4. When it comes to indexing and so on there is no difference
      between lgr grid and the main grid.


      Example:
      --------

      {
         ecl_file_type * restart_data = ecl_file_fread_alloc(restart_filename , true);                      // load some restart info to inspect
         ecl_grid_type * grid         = ecl_grid_alloc(grid_filename , true);                               // bootstrap ecl_grid instance
         stringlist_type * lgr_names  = ecl_grid_alloc_name_list( grid );                                   // get a list of all the LGR names.
      
         printf("Grid:%s has %d a total of %d LGR's \n", grid_filename , stringlist_get_size( lgr_names ));
         for (int lgr_nr = 0; lgr_nr < stringlist_get_size( lgr_names); lgr_nr++) {
            ecl_grid_type * lgr_grid  = ecl_grid_get_lgr( grid , stringlist_iget( lgr_names , lgr_nr ));    // Get the ecl_grid instance of the lgr - by name.
            ecl_kw_type   * pressure_kw;
            int nx,ny,nz,active_size;
            ecl_grid_get_dims( lgr_grid , &nx , &ny , &nz , &active_size);                             // Get some size info from this lgr.
            printf("LGR:%s has %d x %d x %d elements \n",stringlist_iget(lgr_names , lgr_nr ) , nx , ny , nz);

            // OK - now we want to extract the solution vector (pressure) corresponding to this lgr:
            pressure_kw = ecl_file_iget_named_kw( ecl_file , "PRESSURE" , ecl_grid_get_grid_nr( lgr_grid ));
                                                                                      /|\
                                                                                       |
                                                                                       |   
                                                                        We query the lgr_grid instance to find which
                                                                        occurence of the solution data we should look
                                                                        up in the ecl_file instance with restart data. Puuhh!!

            {
               int center_index = ecl_grid_get_global_index3( lgr_grid , nx/2 , ny/2 , nz/2 );          // Ask the lgr_grid to get the index at the center of the lgr grid.
               printf("The pressure in the middle of %s is %g \n", stinglist_iget( lgr_names , lgr_nr ) , ecl_kw_iget_as_double( pressure_kw , center_index ));
            }
         }
         ecl_file_free( restart_data );
         ecl_grid_free( grid );
         stringlist_free( lgr_names );
     }

*/



/*
  About tetraheder decomposition
  ------------------------------

  The table tetraheder_permutations describe how the cells can be
  divided into twelve tetrahedrons. The dimensions in the the table
  are as follows:

   1. The first dimension is the "way" the cell is divided into
      tetrahedrons, there are two different ways. For cells where the
      four point corners on a face are NOT in the same plane, the two
      methods will not give the same result. Which one is "right"??

   2. The second dimension is the tetrahedron number, for each way of
      the two ways there are a total of twelve tetrahedrons.

   3. The third and list dimension is the point number in this
      tetrahedron. When forming a tetrahedron the first input point
      should always be the point corresponding to center of the
      cell. That is not explicit in this table.

   I.e. for instance the third tetrahedron for the first method
   consists of the cells:

        tetraheheder_permutations[0][2] = {0 , 4 , 5}

   in addition to the central point. The value [0..7] correspond the
   the number scheme of the corners in a cell used by ECLIPSE:


       Lower layer:   Upper layer  
                    
         2---3           6---7
         |   |           |   |
         0---1           4---5


   Table entries are ripped from ECLPOST code - file: kvpvos.f in
   klib/
*/


static const int tetrahedron_permutations[2][12][3] = {{{0 , 2 , 6},
                                                        {0 , 4 , 6},
                                                        {0 , 4 , 5},
                                                        {0 , 1 , 5},
                                                        {1 , 3 , 7},
                                                        {1 , 5 , 7},
                                                        {2 , 3 , 7},
                                                        {2 , 6 , 7},
                                                        {0 , 1 , 2},
                                                        {1 , 2 , 3},
                                                        {4 , 5 , 6},
                                                        {5 , 6 , 7}},
                                                       {{0 , 2 , 4},  
                                                        {2 , 4 , 6},  
                                                        {0 , 4 , 1},  
                                                        {4 , 5 , 1},  
                                                        {1 , 3 , 5},  
                                                        {3 , 5 , 7},  
                                                        {2 , 3 , 6},   
                                                        {3 , 6 , 7},   
                                                        {0 , 1 , 3},  
                                                        {0 , 2 , 3},  
                                                        {4 , 5 , 7},  
                                                        {4 , 6 , 7}}};

/*

  The implementation is based on a hierarchy of three datatypes:

   1. ecl_grid   - This is the only exported datatype
   2. ecl_cell   - Internal
   3. point      - Implemented in file point.c

*/


typedef struct ecl_cell_struct  ecl_cell_type;



struct ecl_cell_struct {
  bool                   active;
  int                    active_index;
  point_type            *center;
  point_type            *corner_list[8];
  const ecl_grid_type   *lgr;                /* If this cell is part of an LGR; this will point to a grid instance for that LGR; NULL if not part of LGR. */
  int                    host_cell;          /* The global index of the host cell for an LGR cell, set to -1 for normal cells. */
  bool                   tainted_geometry;   /* Lazy fucking stupid reservoir engineers make invalid grid cells - for kicks?? 
                                                Must keep those cells out of real-world calculations with some hysteric heuristics.*/
};  




#define ECL_GRID_ID 991010
struct ecl_grid_struct {
  UTIL_TYPE_ID_DECLARATION;
  int                   grid_nr;       /* This corresponds to item 4 in GRIDHEAD - 0 for the main grid. */ 
  char                * name;          /* The name of the file for the main grid - name of the LGR for LGRs. */
  int                   ny,nz,nx;
  int                   size;          /* == nx*ny*nz */
  int                   total_active; 
  bool                * visited;       /* Internal helper struct used when searching for index - can be NULL. */
  int                 * index_map;     /* This a list of nx*ny*nz elements, where value -1 means inactive cell .*/
  int                 * inv_index_map; /* This is list of total_active elements - which point back to the index_map. */
  ecl_cell_type      ** cells;         

  char                * parent_name;   /* The name of the parent for a nested LGR - for a LGR descending directly from the main grid this will be NULL. */
  hash_type           * children;      /* A table of LGR children for this grid. */
  const ecl_grid_type * parent_grid;   /* The parent grid for this (lgr) - NULL for the main grid. */      
  const ecl_grid_type * global_grid;   /* The global grid - NULL for the main grid. */

  /*
    The two fields below are for *storing* LGR grid instances. Observe
    that these fields will be NULL for LGR grids, i.e. grids with
    grid_nr > 0. 
  */
  vector_type         * LGR_list;      /* A vector of ecl_grid instances for LGR's - the index corresponds to the grid_nr. */
  hash_type           * LGR_hash;      /* A hash of pointers to ecl_grid instances - for name based lookup of LGR. */
  int                   parent_box[6]; /* Integers i1,i2, j1,j2, k1,k2 of the parent grid region containing this LGR. The indices are INCLUSIVE - zero offset */
                                       /* Not used yet .. */ 
  bool                  use_mapaxes;
  double                unit_x[2];
  double                unit_y[2];
  double                origo[2];
  /*------------------------------:       The fields below this line are used for blocking algorithms - and not allocated by default.*/
  int                    block_dim; /* == 2 for maps and 3 for fields. 0 when not in use. */
  int                    block_size;
  int                    last_block_index;
  double_vector_type  ** values;
};





static void ecl_cell_compare(const ecl_cell_type * c1 , ecl_cell_type * c2, bool * equal) {
  int i;
  if (c1->active != c2->active)
    *equal = false;
  else {
    for (i=0; i < 8; i++)
      point_compare(c1->corner_list[i] , c2->corner_list[i] , equal);
    point_compare(c1->center , c2->center , equal);
  }
}

/*****************************************************************/

static double max2( double x1 , double x2) {
  return (x1 > x2) ? x1 : x2;
}  


static double min2( double x1 , double x2) {
  return  (x1 < x2) ? x1 : x2;
}  


static inline double min4(double x1 , double x2 , double x3 , double x4) {
  return min2( min2(x1 , x2) , min2(x3 , x4 ));
}

static inline double max4(double x1 , double x2 , double x3 , double x4) {
  return max2( max2(x1 , x2) , max2(x3 , x4 ));
}

static inline double max8( double x1 , double x2 , double x3, double x4 , double x5 , double x6 , double x7 , double x8) {
  return max2( max4(x1,x2,x3,x4) , max4(x5,x6,x7,x8));
}

static inline double min8( double x1 , double x2 , double x3, double x4 , double x5 , double x6 , double x7 , double x8) {
  return min2( min4(x1,x2,x3,x4) , min4(x5,x6,x7,x8));
}

/*****************************************************************/

static double ecl_cell_min_z( const ecl_cell_type * cell) {
  return min4( cell->corner_list[0]->z , cell->corner_list[1]->z , cell->corner_list[2]->z , cell->corner_list[3]->z);
}

static double ecl_cell_max_z( const ecl_cell_type * cell ) {
  return max4( cell->corner_list[4]->z , cell->corner_list[5]->z , cell->corner_list[6]->z , cell->corner_list[7]->z );
}


/**
   The grid can be rotated so that it is not safe to consider only one
   plane for the x/y min/max.
*/

static double ecl_cell_min_x( const ecl_cell_type * cell) {
  return min8( cell->corner_list[0]->x , cell->corner_list[1]->x , cell->corner_list[2]->x , cell->corner_list[3]->x,
               cell->corner_list[4]->x , cell->corner_list[5]->x , cell->corner_list[6]->x , cell->corner_list[7]->x );
}


static double ecl_cell_max_x( const ecl_cell_type * cell ) {
  return max8( cell->corner_list[0]->x , cell->corner_list[1]->x , cell->corner_list[2]->x , cell->corner_list[3]->x,
               cell->corner_list[4]->x , cell->corner_list[5]->x , cell->corner_list[6]->x , cell->corner_list[7]->x );
}

static double ecl_cell_min_y( const ecl_cell_type * cell) {
  return min8( cell->corner_list[0]->y , cell->corner_list[1]->y , cell->corner_list[2]->y , cell->corner_list[3]->y,
               cell->corner_list[4]->y , cell->corner_list[5]->y , cell->corner_list[6]->y , cell->corner_list[7]->y );
}


static double ecl_cell_max_y( const ecl_cell_type * cell ) {
  return max8( cell->corner_list[0]->y , cell->corner_list[1]->y , cell->corner_list[2]->y , cell->corner_list[3]->y,
               cell->corner_list[4]->y , cell->corner_list[5]->y , cell->corner_list[6]->y , cell->corner_list[7]->y );
}



/**
   The problem is that some EXTREMELY FUCKING STUPID reservoir
   engineers purpousely have made grids with invalid cells. Typically
   the cells accomodating numerical AQUIFERS are located at an utm
   position (0,0).

   Cells which have some pillars located in (0,0) and some cells
   located among the rest of the grid become completely warped - with
   insane volumes, parts of the reservoir volume doubly covered, and
   so on.
   
   To keep these cells out of the real-world (i.e. involving utm
   coordinates) computations they are marked as 'tainted' in this
   function. The tainting procedure is completely heuristic, and
   probably wrong.
*/


static void ecl_cell_taint_cell( ecl_cell_type * cell ) {
  int c;
  for (c = 0; c < 8; c++) {
    const point_type *p = cell->corner_list[c];
    if ((p->x == 0) && (p->y == 0))
      cell->tainted_geometry = true;
  }
}


/*****************************************************************/


/**
   Observe that when allocating based on a GRID file not all cells are
   necessarily accessed beyond this function. In general not all cells
   will have a COORDS/CORNERS section in the GRID file.
*/


static ecl_cell_type * ecl_cell_alloc(void) {
  ecl_cell_type * cell = util_malloc(sizeof * cell , __func__);
  cell->active         = false;
  cell->lgr            = NULL;
  cell->center         = point_alloc_empty();
  for (int i=0; i < 8; i++)
    cell->corner_list[i] = point_alloc_empty();

  cell->tainted_geometry = false;
  cell->host_cell        = -1;
  return cell;
}

static void ecl_cell_install_lgr( ecl_cell_type * cell , const ecl_grid_type * lgr_grid) {
  cell->lgr       = lgr_grid;
}


static void ecl_cell_fprintf( const ecl_cell_type * cell , FILE * stream ) {
  int i;
  for (i=0; i < 7; i++) {
    printf("\nCorner[%d] => ",i);
    point_fprintf( cell->corner_list[i] , stdout );
  }
  fprintf(stream , "-----------------------------------\n");
}




static void ecl_cell_init_tetrahedron( const ecl_cell_type * cell , tetrahedron_type * tet , int method_nr , int tetrahedron_nr) {
  int point0 = tetrahedron_permutations[ method_nr ][ tetrahedron_nr ][ 0 ];
  int point1 = tetrahedron_permutations[ method_nr ][ tetrahedron_nr ][ 1 ];
  int point2 = tetrahedron_permutations[ method_nr ][ tetrahedron_nr ][ 2 ];

  tetrahedron_set_shared( tet , cell->center , cell->corner_list[ point0 ] , cell->corner_list[point1] , cell->corner_list[point2]);
}



static double ecl_cell_get_volume( const ecl_cell_type * cell ) {
  tetrahedron_type tet;
  int              itet;
  double           volume = 0;
  for (itet = 0; itet < 12; itet++) {
    /* 
       Using both tetrahedron decompositions - gives good agreement
       with PORV from ECLIPSE INIT files.
    */
    ecl_cell_init_tetrahedron( cell , &tet , 0 , itet );
    volume += tetrahedron_volume( &tet );

    ecl_cell_init_tetrahedron( cell , &tet , 1 , itet );
    volume += tetrahedron_volume( &tet );

  }
  
  return volume * 0.5;
}




static double triangle_area(double x1 , double y1 , double x2 , double y2 , double x3 ,double y3) {
  return fabs(x1*y2 + x2*y3 + x3*y1 - x1*y3 - x3*y2 - x2*y1)*0.5;
}


static bool triangle_contains(const point_type *p0 , const point_type * p1 , const point_type *p2 , double x , double y) {
  double epsilon = 1e-10;

  double VT = triangle_area(p0->x , p0->y,
                            p1->x , p1->y,
                            p2->x , p2->y);

  if (VT < epsilon)  /* Zero size cells do not contain anything. */
    return false;
  {
    double V1 = triangle_area(p0->x , p0->y,
                              p1->x , p1->y,
                              x     , y); 
    
    double V2 = triangle_area(p0->x , p0->y,
                              x     , y,
                              p2->x , p2->y);
    
    double V3 = triangle_area(x     , y,
                              p1->x , p1->y,
                              p2->x , p2->y);
    
    
    if (fabs( VT - (V1 + V2 + V3 )) < epsilon)
      return true;
    else
      return false;
  }
}




/* 
   If the layer defined by the cell corners 0-1-2-3 (lower == true) or
   4-5-6-7 (lower == false) contain the point (x,y) the function will
   return true - otehrwise false.
   
   The function works by dividing the cell face into two triangles,
   which are checked one at a time with the function
   triangle_contains().
*/


static bool ecl_cell_layer_contains_xy( const ecl_cell_type * cell , bool lower_layer , double x , double y) {
  if (cell->tainted_geometry)
    return false;
  {
    point_type * p0,*p1,*p2,*p3;
    {
      int corner_offset;
      if (lower_layer) 
        corner_offset = 0;
      else
        corner_offset = 4;
      
      p0 = cell->corner_list[corner_offset + 0];
      p1 = cell->corner_list[corner_offset + 1];
      p2 = cell->corner_list[corner_offset + 2];
      p3 = cell->corner_list[corner_offset + 3];
    }
    
    if (triangle_contains(p0,p1,p2,x,y))
      return true;
    else
      return triangle_contains(p1,p2,p3,x,y);
  }
}

/*
Deeper layer: (larger (negative) z values).
------------

  6---7
  |   |
  4---5


  2---3
  |   |
  0---1



*/

  


static bool ecl_cell_contains_point( const ecl_cell_type * cell , const point_type * p) {
  /*
    1. First check if the point z value is below the deepest point of
       the cell, or above the shallowest => Return False.

    2. [Should do similar fast checks in x/y direction, but that
        requires proper mapaxes support. ]

    3. Full geometric verification.
  */

  /* 
     OK -the point is in the volume inside the large rectangle, and
     outside the small rectangle. Then we must use the full
     tetrahedron decomposition to determine.
  */
  if (cell->tainted_geometry) 
    return false;
  
  if (p->z < ecl_cell_min_z( cell ))
    return false;
  
  if (p->z > ecl_cell_max_z( cell ))
    return false;

  if (p->x < ecl_cell_min_x( cell ))
    return false;
  
  if (p->x > ecl_cell_max_x( cell ))
    return false;
  
  if (p->y < ecl_cell_min_y( cell ))
    return false;
  
  if (p->y > ecl_cell_max_y( cell ))
    return false;
  
  

  {
    const int method   = 0;
    int tetrahedron_nr = 0;
    tetrahedron_type tet;
    
    if (ecl_cell_get_volume( cell ) > 0) {
      /* Does never exit from this loop - only returns from the whole function. */
      while (true) {   
        ecl_cell_init_tetrahedron( cell , &tet , method , tetrahedron_nr );
        if (tetrahedron_contains( &tet , p )) 
          return true;
        
        tetrahedron_nr++;
        if (tetrahedron_nr == 12)
          return false;  /* OK - cell did not contain point. */
      } 
    } 
  }
  util_abort("%s: Internal error - should not be here \n",__func__);
  return false;
}



static void ecl_cell_free(ecl_cell_type * cell) {
  point_free( cell->center );
  {
    int i;
    for (i=0; i < 8; i++)
      point_free( cell->corner_list[i] );
  }
  free(cell);
}

/* End of cell implementation                                    */
/*****************************************************************/
/* Starting on the ecl_grid proper implementation                */

UTIL_SAFE_CAST_FUNCTION(ecl_grid , ECL_GRID_ID);

/**
   This function allocates the internal index_map and inv_index_map fields.
*/




/**
   This function uses heuristics (ahhh - I hate it) in an attempt to
   mark cells with fucked geometry - see further comments in the
   function ecl_cell_taint_cell() which actually does it.
*/

static void ecl_grid_taint_cells( ecl_grid_type * ecl_grid ) {
  int index;
  for (index = 0; index < ecl_grid->size; index++) {
    ecl_cell_type * cell = ecl_grid->cells[index];
    ecl_cell_taint_cell( cell );
  }
}


/**
   Will create a new blank grid instance. If the global_grid argument
   is != NULL the newly created grid instance will copy the mapaxes
   transformations; and set the global_grid pointer of the new grid
   instance. Apart from that no further lgr-relationsip initialisation
   is performed.
*/

static ecl_grid_type * ecl_grid_alloc_empty(ecl_grid_type * global_grid , int nx , int ny , int nz, int grid_nr) {
  ecl_grid_type * grid = util_malloc(sizeof * grid , __func__);
  UTIL_TYPE_ID_INIT(grid , ECL_GRID_ID);
  grid->nx          = nx;
  grid->ny          = ny;
  grid->nz          = nz;
  grid->size        = nx*ny*nz;
  grid->grid_nr     = grid_nr;
  grid->global_grid = global_grid;

  grid->visited       = NULL;
  grid->inv_index_map = NULL;
  grid->index_map     = NULL;
  grid->cells         = util_malloc(nx*ny*nz * sizeof * grid->cells , __func__);

  if (global_grid != NULL) {
    /* This is an lgr instance, and we inherit the global grid
       transformations from the main grid. */
    grid->unit_x[0]     = global_grid->unit_x[0];  
    grid->unit_x[0]     = global_grid->unit_x[0];  
    grid->unit_y[0]     = global_grid->unit_y[0];  
    grid->unit_y[0]     = global_grid->unit_y[0];  
    grid->origo[0]      = global_grid->origo[0];   
    grid->origo[1]      = global_grid->origo[1];   
    grid->use_mapaxes   = global_grid->use_mapaxes;
  } else {
    grid->unit_x[0]     = 1;
    grid->unit_x[0]     = 0;
    grid->unit_y[0]     = 0;
    grid->unit_y[0]     = 1;
    grid->origo[0]      = 0;
    grid->origo[1]      = 0;
    grid->use_mapaxes   = false;
  }

  {
    int i;
    for (i=0; i < grid->size; i++)
      grid->cells[i] = ecl_cell_alloc();
  }
  grid->block_dim      = 0;
  grid->values         = NULL;
  if (grid_nr == 0) {  /* This is the main grid */
    grid->LGR_list = vector_alloc_new(); 
    vector_append_ref( grid->LGR_list , grid ); /* Adding a 'self' pointer as first inistance - without destructor! */
    grid->LGR_hash = hash_alloc();
  } else {
    grid->LGR_list = NULL;
    grid->LGR_hash = NULL;
  }
  grid->name        = NULL;
  grid->parent_name = NULL;
  grid->parent_grid = NULL;
  grid->children    = hash_alloc();
  return grid;
}


static void ecl_grid_set_center(ecl_grid_type * ecl_grid) {
  int c , i;
  for (i=0; i < ecl_grid->size; i++) {
    ecl_cell_type * cell = ecl_grid->cells[i];
    point_set(cell->center , 0 , 0 , 0);
    for (c = 0; c < 8; c++)
      point_inplace_add(cell->center , cell->corner_list[c]);
    point_inplace_scale(cell->center , 1.0 / 8.0);
  }
}




static inline int ecl_grid_get_global_index__(const ecl_grid_type * ecl_grid , int i , int j , int k) {
  return i + j * ecl_grid->nx + k * ecl_grid->nx * ecl_grid->ny;
}


static void ecl_grid_set_cell_EGRID(ecl_grid_type * ecl_grid , int i, int j , int k , double x[4][2] , double y[4][2] , double z[4][2] , const int * actnum) {

  const int global_index   = ecl_grid_get_global_index__(ecl_grid , i , j  , k );
  ecl_cell_type * cell     = ecl_grid->cells[global_index];
  int ip , iz;

  for (iz = 0; iz < 2; iz++) {
    for (ip = 0; ip < 4; ip++) {
      int c = ip + iz * 4;
      point_set(cell->corner_list[c] , x[ip][iz] , y[ip][iz] , z[ip][iz]);
      
      if (ecl_grid->use_mapaxes)
        point_mapaxes_transform( cell->corner_list[c] , ecl_grid->origo , ecl_grid->unit_x , ecl_grid->unit_y );
    }
  }


  /*
    For normal runs actnum will be 1 for active cells,
    for dual porosity models it can also be 2 and 3.
  */
  if (actnum[global_index] > 0)
    cell->active = true;
}


static void ecl_grid_set_cell_GRID(ecl_grid_type * ecl_grid , const ecl_kw_type * coords_kw , const ecl_kw_type * corners_kw) {
  const int   * coords  = ecl_kw_get_int_ptr(coords_kw);
  const float * corners = ecl_kw_get_float_ptr(corners_kw);
  const int i  = coords[0]; /* ECLIPSE 1 offset */
  const int j  = coords[1];
  const int k  = coords[2];
  const int global_index   = ecl_grid_get_global_index__(ecl_grid , i - 1, j - 1 , k - 1);
  ecl_cell_type * cell     = ecl_grid->cells[global_index];

  /* The coords keyword can optionally contain 4,5 or 7 elements:

        coords[0..2] = i,j,k
        coords[3]    = global_cell number (not used here)
        ----
        coords[4]    = 1,0 for active/inactive cells
        coords[5]    = 0 for normal cells, icell of host cell for LGR cell.
        coords[6]    = 0 for normal cells, coarsening group for coarsened cell [NOT TREATED YET].

     If coords[4] is not present it is assumed that the cell is active.
  */

  {
    int c;
    int coords_size = ecl_kw_get_size(coords_kw);

    switch(coords_size) {
    case(4):                /* All cells active */
      cell->active = true;
      break;
    case(5):                /* Only spesific cells active - no LGR */
      cell->active  = (coords[4] == 1) ? true : false;
      break;
    case(7):
      cell->active    = (coords[4] == 1) ? true : false;
      cell->host_cell = coords[5] - 1;
      break;
    }
    
    for (c = 0; c < 8; c++) {
      point_set(cell->corner_list[c] , corners[3*c] , corners[3*c + 1] , corners[3*c + 2]);
      if (ecl_grid->use_mapaxes)
        point_mapaxes_transform( cell->corner_list[c] , ecl_grid->origo , ecl_grid->unit_x , ecl_grid->unit_y );
    }

  }
}

/**
   The functions ecl_grid_set_active_index() must be called
   immediately prior to calling this function, to ensure that
   ecl_grid->total_active is correct.
*/
static void ecl_grid_realloc_index_map(ecl_grid_type * ecl_grid) {
  ecl_grid->index_map     = util_realloc(ecl_grid->index_map     , ecl_grid->size         * sizeof * ecl_grid->index_map     , __func__);
  ecl_grid->inv_index_map = util_realloc(ecl_grid->inv_index_map , ecl_grid->total_active * sizeof * ecl_grid->inv_index_map , __func__);
  {
    int index;
    for (index = 0; index < ecl_grid->size; index++) {
      const ecl_cell_type * cell = ecl_grid->cells[index];
      if (cell->active) {
        ecl_grid->index_map[index] = cell->active_index;
        ecl_grid->inv_index_map[cell->active_index] = index;
      } else
        ecl_grid->index_map[index] = -1;
    }
  }
}
  

static void ecl_grid_set_active_index(ecl_grid_type * ecl_grid) {
  int i,j,k;
  int active_index = 0;

  for (k=0; k < ecl_grid->nz; k++)
    for (j=0; j < ecl_grid->ny; j++)
      for (i=0; i < ecl_grid->nx; i++) {
        const int global_index   = ecl_grid_get_global_index__(ecl_grid , i , j , k );
        ecl_cell_type * cell     = ecl_grid->cells[global_index];
        if (cell->active) {
          cell->active_index = active_index;
          active_index++;
        } else
          cell->active_index = -1;
      }

  ecl_grid->total_active = active_index;
}


static void ecl_grid_update_index( ecl_grid_type * ecl_grid) {
  ecl_grid_set_active_index(ecl_grid);
  ecl_grid_realloc_index_map(ecl_grid);
}


static void ecl_grid_pillar_cross_planes(const point_type * pillar , const double *z , double *x , double *y) {
  double e_x , e_y , e_z;
  int k;
  e_x = pillar[1].x - pillar[0].x;
  e_y = pillar[1].y - pillar[0].y;
  e_z = pillar[1].z - pillar[0].z;

  for (k=0; k < 2; k++) {
    double t = (z[k] -  pillar[0].z) / e_z;
    x[k] = pillar[0].x + t * e_x;
    y[k] = pillar[0].y + t * e_y;
  }
}


/**
   This function must be run before the cell coordinates are calculated.

   This function is only called for the main grid instance, and not
   for LGR's. Do not really know if that is correct; probably the LGR
   should inherit the mapaxes transform of the parent?
*/


static void ecl_grid_init_mapaxes( ecl_grid_type * ecl_grid , const float * mapaxes) {
  if (ecl_grid->global_grid != NULL)
    util_abort("%s: Hmmmm - this is a major fuck up; trying to grid transformation data from MAPAXES for a subgrid(lgr)\n",__func__);
  {
    const double unit_y[2] = {mapaxes[0] - mapaxes[2] , mapaxes[1] - mapaxes[3]};
    const double unit_x[2] = {mapaxes[4] - mapaxes[2] , mapaxes[5] - mapaxes[3]};
    
    {
      double norm_x = 1.0/sqrt( unit_x[0]*unit_x[0] + unit_x[1]*unit_x[1] );
      double norm_y = 1.0/sqrt( unit_y[0]*unit_y[0] + unit_y[1]*unit_y[1] );
      
      ecl_grid->unit_x[0] = unit_x[0] * norm_x;
      ecl_grid->unit_x[1] = unit_x[1] * norm_x;
      ecl_grid->unit_y[0] = unit_y[0] * norm_y;
      ecl_grid->unit_y[1] = unit_y[1] * norm_y;
    }
    ecl_grid->origo[0] = mapaxes[2];
    ecl_grid->origo[1] = mapaxes[3];
    
    ecl_grid->use_mapaxes = true;
  }
}





/**
   This function will add a ecl_grid instance as a LGR to the main
   grid. The LGR grid as added to two different structures of the main
   grid:

    1. In the main_grid->LGR_list the LGR instances are inserted in
       order of occurence in the GRID file. The following equalities
       should apply:

          occurence number in file == lgr_grid->grid_nr == GRIDHEAD(4) for lgr == index in the LGR_list vector
  
       When installed in the LGR_list vector the lgr grid is installed
       with a destructor, i.e. the grid is destroyed when the vector
       is destroyed.

    2. In the main->LGR_hash the lgr instance is installed with the
       LGRNAME as key. Only a reference is installed in the hash
       table. 

    Observe that this is in principle somewhat different from the
    install functions below; here the lgr is added to the top level
    grid (i.e. the main grid) which has the storage responsability of
    all the lgr instances. The cell->lgr relationship is established
    in the _install_EGRID / install_GRID functions further down.
*/


static void ecl_grid_add_lgr( ecl_grid_type * main_grid , ecl_grid_type * lgr_grid) {
  int next_grid_nr = vector_get_size( main_grid->LGR_list );
  if (next_grid_nr != lgr_grid->grid_nr) 
    util_abort("%s: index based insertion of LGR grid failed. next_grid_nr:%d  lgr->grid_nr:%d \n",__func__ , next_grid_nr , lgr_grid->grid_nr);
  {
    vector_append_owned_ref( main_grid->LGR_list , lgr_grid , ecl_grid_free__);
    hash_insert_ref( main_grid->LGR_hash , lgr_grid->name , lgr_grid);
  }
}



/**
   This function will set the lgr pointer of the relevant cells in the
   host grid to point to the lgr_grid. Observe that the ecl_cell_type
   instances do *NOT* own the lgr_grid - all lgr_grid instances are
   owned by the main grid.
*/

static void ecl_grid_install_lgr_EGRID(ecl_grid_type * host_grid , ecl_grid_type * lgr_grid , const int * hostnum) {
  int global_lgr_index;

  for (global_lgr_index = 0; global_lgr_index < lgr_grid->size; global_lgr_index++) {
    ecl_cell_type * lgr_cell = lgr_grid->cells[global_lgr_index];
    if (lgr_cell->active) {
      ecl_cell_type * host_cell = host_grid->cells[ hostnum[ global_lgr_index ] ];
      ecl_cell_install_lgr( host_cell , lgr_grid );

      lgr_cell->host_cell = hostnum[ global_lgr_index ] - 1;  /* HOSTNUM has offset 1 (I think ... ) */
    }
  }
  hash_insert_ref( host_grid->children , lgr_grid->name , lgr_grid);
  lgr_grid->parent_grid = host_grid;
}


/**
   Similar to ecl_grid_install_lgr_EGRID for GRID based instances. 
*/
static void ecl_grid_install_lgr_GRID(ecl_grid_type * host_grid , const ecl_grid_type * lgr_grid) {
  int global_lgr_index;
  
  for (global_lgr_index = 0; global_lgr_index < lgr_grid->size; global_lgr_index++) {
    ecl_cell_type * lgr_cell = lgr_grid->cells[global_lgr_index];
    if (lgr_cell->active) {
      ecl_cell_type * host_cell = host_grid->cells[ lgr_cell->host_cell ];
      ecl_cell_install_lgr( host_cell , lgr_grid );
    }
  }
}



/**
   Sets the name of the lgr AND the name of the parent, if this is a
   nested LGR. For normal LGR descending directly from the coarse grid
   the parent_name is set to NULL.
*/
   

static void ecl_grid_set_lgr_name_EGRID(ecl_grid_type * lgr_grid , const ecl_file_type * ecl_file , int grid_nr) {
  ecl_kw_type * lgrname_kw = ecl_file_iget_named_kw( ecl_file , "LGR" , grid_nr - 1);
  lgr_grid->name = util_alloc_strip_copy( ecl_kw_iget_ptr( lgrname_kw , 0) );  /* Trailing zeros are stripped away. */
  if (ecl_file_has_kw( ecl_file , "LGRPARNT")) {
    ecl_kw_type * parent_kw = ecl_file_iget_named_kw( ecl_file , "LGRPARNT" , grid_nr - 1);
    char * parent = util_alloc_strip_copy( ecl_kw_iget_ptr( parent_kw , 0));

    if (strlen( parent ) > 0) 
      lgr_grid->parent_name = parent;
    else  /* lgr_grid->parent has been initialized to NULL */
      free( parent );
  }
}

/**
   Sets the name of the lgr AND the name of the parent, if this is a
   nested LGR. For LGR descending directly from the parent ECLIPSE
   will supply 'GLOBAL' (whereas for EGRID it will return '' -
   cool?). Anyway GLOBAL -> NULL.
*/

static void ecl_grid_set_lgr_name_GRID(ecl_grid_type * lgr_grid , const ecl_file_type * ecl_file , int grid_nr) {
  ecl_kw_type * lgr_kw = ecl_file_iget_named_kw( ecl_file , "LGR" , grid_nr - 1);
  lgr_grid->name = util_alloc_strip_copy( ecl_kw_iget_ptr( lgr_kw , 0) );  /* Trailing zeros are stripped away. */
  {
    char * parent = util_alloc_strip_copy( ecl_kw_iget_ptr( lgr_kw , 1));
    if ((strlen(parent) == 0) || (strcmp(parent , "GLOBAL") == 0))
      free( parent );
    else
      lgr_grid->parent_name = parent;
  }
}



/**
   This function can in principle be called by several threads with
   different [j1, j2) intervals to speed things up a bit.  
*/

static void ecl_grid_init_GRDECL_data__(ecl_grid_type * ecl_grid , int j1 , int j2 , const float * zcorn , const float * coord , const int * actnum) {
  const int nx = ecl_grid->nx;
  const int ny = ecl_grid->ny;
  const int nz = ecl_grid->nz;
  point_type pillars[4][2];
  int i,j,k;
  
  for (j=j1; j < j2; j++) {
    for (i=0; i < nx; i++) {
      int pillar_index[4];
      int ip;
      pillar_index[0] = 6 * ( j      * (nx + 1) + i    );
      pillar_index[1] = 6 * ( j      * (nx + 1) + i + 1);
      pillar_index[2] = 6 * ((j + 1) * (nx + 1) + i    );
      pillar_index[3] = 6 * ((j + 1) * (nx + 1) + i + 1);

      for (ip = 0; ip < 4; ip++) {
        int index = pillar_index[ip];
        point_set(&pillars[ip][0] , coord[index] , coord[index + 1] , coord[index + 2]);
        
        index += 3;
        point_set(&pillars[ip][1] , coord[index] , coord[index + 1] , coord[index + 2]);
      }


      for (k=0; k < nz; k++) {
        double x[4][2];
        double y[4][2];
        double z[4][2];
        int c;

        for (c = 0; c < 2; c++) {
          z[0][c] = zcorn[k*8*nx*ny + j*4*nx + 2*i            + c*4*nx*ny];
          z[1][c] = zcorn[k*8*nx*ny + j*4*nx + 2*i  +  1      + c*4*nx*ny];
          z[2][c] = zcorn[k*8*nx*ny + j*4*nx + 2*nx + 2*i     + c*4*nx*ny];
          z[3][c] = zcorn[k*8*nx*ny + j*4*nx + 2*nx + 2*i + 1 + c*4*nx*ny];
        }

        for (ip = 0; ip <  4; ip++)
          ecl_grid_pillar_cross_planes(pillars[ip] , z[ip] , x[ip] , y[ip]);

        ecl_grid_set_cell_EGRID(ecl_grid , i , j , k , x , y , z , actnum);
      }
    }
  }
}


/*
  2---3
  |   |
  0---1
*/

static ecl_grid_type * ecl_grid_alloc_GRDECL_data__(ecl_grid_type * global_grid , int nx , int ny , int nz , const float * zcorn , const float * coord , const int * actnum, const float * mapaxes, int grid_nr) {
  ecl_grid_type * ecl_grid = ecl_grid_alloc_empty(global_grid , nx,ny,nz,grid_nr);

  if (mapaxes != NULL)
    ecl_grid_init_mapaxes( ecl_grid , mapaxes );
  ecl_grid_init_GRDECL_data__( ecl_grid , 0 , ny , zcorn , coord , actnum);
    
  ecl_grid_set_center( ecl_grid );
  ecl_grid_update_index( ecl_grid );
  ecl_grid_taint_cells( ecl_grid );
  return ecl_grid;
}

/*
  If you create/load data for the various fields, this function can be
  used to create a GRID instance, without going through a GRID/EGRID
  file - currently the implementation does not support the creation of
  a lgr hierarchy.
*/

ecl_grid_type * ecl_grid_alloc_GRDECL_data(int nx , int ny , int nz , const float * zcorn , const float * coord , const int * actnum, const float * mapaxes) {
  return ecl_grid_alloc_GRDECL_data__(NULL , nx , ny , nz , zcorn , coord , actnum , mapaxes , 0);
}

static ecl_grid_type * ecl_grid_alloc_GRDECL_kw__(ecl_grid_type * global_grid ,  
                                                  const ecl_kw_type * gridhead_kw , 
                                                  const ecl_kw_type * zcorn_kw , 
                                                  const ecl_kw_type * coord_kw , 
                                                  const ecl_kw_type * actnum_kw , 
                                                  const ecl_kw_type * mapaxes_kw ,   /* Can be NULL */
                                                  int grid_nr) {
  
  int gtype, nx,ny,nz;
  
  gtype   = ecl_kw_iget_int(gridhead_kw , 0);
  nx      = ecl_kw_iget_int(gridhead_kw , 1);
  ny      = ecl_kw_iget_int(gridhead_kw , 2);
  nz      = ecl_kw_iget_int(gridhead_kw , 3);
  if (gtype != 1)
    util_abort("%s: gtype:%d fatal error when loading grid - must have corner point grid - aborting\n",__func__ , gtype );
  {
    const float * mapaxes_data = NULL;

    if (mapaxes_kw != NULL)
      mapaxes_data = ecl_kw_get_float_ptr( mapaxes_kw );
    
    return ecl_grid_alloc_GRDECL_data__(global_grid , 
                                        nx , ny , nz , 
                                        ecl_kw_get_float_ptr(zcorn_kw) , 
                                        ecl_kw_get_float_ptr(coord_kw) , 
                                        ecl_kw_get_int_ptr(actnum_kw) , 
                                        mapaxes_data, 
                                        grid_nr);
  }
}


/**
   If you create/load ecl_kw instances for the various fields, this
   function can be used to create a GRID instance, without going
   through a GRID/EGRID file.
*/

ecl_grid_type * ecl_grid_alloc_GRDECL_kw( const ecl_kw_type * gridhead_kw , const ecl_kw_type * zcorn_kw , const ecl_kw_type * coord_kw , const ecl_kw_type * actnum_kw , const ecl_kw_type * mapaxes_kw ) {
  return ecl_grid_alloc_GRDECL_kw__(NULL , gridhead_kw , zcorn_kw , coord_kw , actnum_kw , mapaxes_kw , 0);
}



/**
   Creating a grid based on a EGRID file is a three step process:

    1. Load the file and extracte the keywords.
    2. Call xx_alloc_GRDECL_kw__() to build grid based on keywords.
    3. Call xx_alloc_GRDECL_data__() to build the grid based on keyword data.
    
   The point is that external scope can create grid based on both a
   list of keywords, and actual data - in addition to the normal loading
   of a full file.
*/


static ecl_grid_type * ecl_grid_alloc_EGRID__( ecl_grid_type * main_grid , const ecl_file_type * ecl_file , int grid_nr) {
  ecl_kw_type * gridhead_kw  = ecl_file_iget_named_kw( ecl_file , "GRIDHEAD" , grid_nr);
  ecl_kw_type * zcorn_kw     = ecl_file_iget_named_kw( ecl_file , "ZCORN"     , grid_nr);
  ecl_kw_type * coord_kw     = ecl_file_iget_named_kw( ecl_file , "COORD"     , grid_nr);
  ecl_kw_type * actnum_kw    = ecl_file_iget_named_kw( ecl_file , "ACTNUM"    , grid_nr);
  ecl_kw_type * mapaxes_kw   = NULL; 
  
  if ((grid_nr == 0) && (ecl_file_has_kw( ecl_file , "MAPAXES"))) 
    mapaxes_kw   = ecl_file_iget_named_kw( ecl_file , "MAPAXES" , grid_nr);
  {
    ecl_grid_type * ecl_grid = ecl_grid_alloc_GRDECL_kw__( main_grid , 
                                                           gridhead_kw , 
                                                           zcorn_kw , 
                                                           coord_kw , 
                                                           actnum_kw , 
                                                           mapaxes_kw , 
                                                           grid_nr );

    
    if (grid_nr > 0) ecl_grid_set_lgr_name_EGRID(ecl_grid , ecl_file , grid_nr);
    return ecl_grid;
  }
}



static ecl_grid_type * ecl_grid_alloc_EGRID(const char * grid_file ) {
  ecl_file_enum   file_type;
  file_type = ecl_util_get_file_type(grid_file , NULL , NULL);
  if (file_type != ECL_EGRID_FILE)
    util_abort("%s: %s wrong file type - expected .EGRID file - aborting \n",__func__ , grid_file);
  {
    ecl_file_type * ecl_file   = ecl_file_fread_alloc( grid_file );
    int num_grid               = ecl_file_get_num_named_kw( ecl_file , "GRIDHEAD" );
    ecl_grid_type * main_grid  = ecl_grid_alloc_EGRID__( NULL , ecl_file , 0 );
    
    for (int grid_nr = 1; grid_nr < num_grid; grid_nr++) {
      ecl_grid_type * lgr_grid = ecl_grid_alloc_EGRID__( main_grid , ecl_file , grid_nr );
      ecl_grid_add_lgr( main_grid , lgr_grid );
      {
        ecl_grid_type * host_grid;
        ecl_kw_type   * hostnum_kw = ecl_file_iget_named_kw( ecl_file , "HOSTNUM" , grid_nr - 1);
        if (lgr_grid->parent_name == NULL)
          host_grid = main_grid;
        else 
          host_grid = ecl_grid_get_lgr( main_grid , lgr_grid->parent_name );
          
        ecl_grid_install_lgr_EGRID( host_grid , lgr_grid , ecl_kw_get_int_ptr( hostnum_kw) );
      }
    }
    main_grid->name = util_alloc_string_copy( grid_file );
    ecl_file_free( ecl_file );
    return main_grid;
  }
}










/* 
   
*/

static ecl_grid_type * ecl_grid_alloc_GRID__(ecl_grid_type * global_grid , const ecl_file_type * ecl_file , int * cell_offset , int grid_nr) {
  int index,nx,ny,nz;
  ecl_grid_type * grid;
  ecl_kw_type * dimens_kw   = ecl_file_iget_named_kw( ecl_file , "DIMENS" , grid_nr);
  nx   = ecl_kw_iget_int(dimens_kw , 0);
  ny   = ecl_kw_iget_int(dimens_kw , 1);
  nz   = ecl_kw_iget_int(dimens_kw , 2);
  grid = ecl_grid_alloc_empty(global_grid , nx , ny , nz, grid_nr);
  
  /*
    Possible LGR cells will follow *AFTER* the first nx*ny*nz cells;
    the loop stops at nx*ny*nz. Additionally the LGR cells should be
    discarded (by checking coords[5]) in the
    ecl_grid_set_cell_GRID() function.
  */
  
  if ((grid_nr == 0) && (ecl_file_has_kw( ecl_file , "MAPAXES"))) {
    const ecl_kw_type * mapaxes_kw = ecl_file_iget_named_kw( ecl_file , "MAPAXES" , grid_nr);
    ecl_grid_init_mapaxes( grid , ecl_kw_get_float_ptr( mapaxes_kw) );
  }
  
  {
    int num_coords = ecl_file_get_num_named_kw( ecl_file , "COORDS" );
    for (index = 0; index < num_coords; index++) {
      ecl_kw_type * coords_kw  = ecl_file_iget_named_kw(ecl_file , "COORDS"  , index + (*cell_offset));
      ecl_kw_type * corners_kw = ecl_file_iget_named_kw(ecl_file , "CORNERS" , index + (*cell_offset));
      ecl_grid_set_cell_GRID(grid , coords_kw , corners_kw);
    }
    (*cell_offset) += num_coords;
  }

  ecl_grid_set_center(grid);
  ecl_grid_update_index( grid );
  if (grid_nr > 0) ecl_grid_set_lgr_name_GRID(grid , ecl_file , grid_nr);
  ecl_grid_taint_cells( grid );
  return grid;
}



static ecl_grid_type * ecl_grid_alloc_GRID(const char * grid_file) {

  ecl_file_enum   file_type;
  file_type = ecl_util_get_file_type(grid_file , NULL , NULL);
  if (file_type != ECL_GRID_FILE)
    util_abort("%s: %s wrong file type - expected .GRID file - aborting \n",__func__ , grid_file);

  {
    int cell_offset = 0;
    ecl_file_type * ecl_file  = ecl_file_fread_alloc( grid_file );
    int num_grid              = ecl_file_get_num_named_kw( ecl_file , "DIMENS");
    ecl_grid_type * main_grid = ecl_grid_alloc_GRID__(NULL , ecl_file , &cell_offset , 0);
    for (int grid_nr = 1; grid_nr < num_grid; grid_nr++) {
      ecl_grid_type * lgr_grid = ecl_grid_alloc_GRID__(main_grid , ecl_file , &cell_offset , grid_nr );
      ecl_grid_add_lgr( main_grid , lgr_grid );
      {
        ecl_grid_type * host_grid;
        if (lgr_grid->parent_name == NULL)
          host_grid = main_grid;
        else 
          host_grid = ecl_grid_get_lgr( main_grid , lgr_grid->parent_name );
          
        ecl_grid_install_lgr_GRID( host_grid , lgr_grid );
      }
    }
    main_grid->name = util_alloc_string_copy( grid_file );
    ecl_file_free( ecl_file );
    return main_grid;
  }
}
                                 



/**
   This function will allocate a ecl_grid instance. As input it takes
   a filename, which can be both a GRID file and an EGRID file (both
   formatted and unformatted).

   When allocating based on an EGRID file the COORDS, ZCORN and ACTNUM
   keywords are extracted, and the ecl_grid_alloc_GRDECL() function is
   called with these keywords. This function can be called directly
   with these keywords.
*/

ecl_grid_type * ecl_grid_alloc(const char * grid_file ) {
  ecl_file_enum    file_type;
  ecl_grid_type  * ecl_grid = NULL;

  file_type = ecl_util_get_file_type(grid_file , NULL ,  NULL);
  if (file_type == ECL_GRID_FILE)
    ecl_grid = ecl_grid_alloc_GRID(grid_file );
  else if (file_type == ECL_EGRID_FILE)
    ecl_grid = ecl_grid_alloc_EGRID(grid_file);
  else
    util_abort("%s must have .GRID or .EGRID file - %s not recognized \n", __func__ , grid_file);
  
  return ecl_grid;
}



/**
   Will load the grid corresponding to the input @input_case;
   depending on the value of @input_case many different paths will be
   tried:

   1 case_input - an existing GRID/EGRID file: Just load the file -
     with no further ado.

   2 case_input - an existing ECLIPSE file which is not a grid file;
     if it has definite formatted/unformatted status look only for
     those GRID/EGRID with the same formatted/unformatted status.
    
   3 case_input is only an ECLIPSE base, look for
     formatted/unformatted files with the correct basename.


   For cases 2 & 3 the function will look for files in the following order:

      BASE.EGRID   BASE.GRID   BASE.FEGRID   BASE.FGRID

   and stop with the first success. Will return NULL if no GRID/EGRID
   files can be found.
*/




char * ecl_grid_alloc_case_filename( const char * case_input ) {
  ecl_file_enum    file_type;
  bool             fmt_file;
  file_type = ecl_util_get_file_type( case_input , &fmt_file ,  NULL);
  
  if (file_type == ECL_GRID_FILE)
    return util_alloc_string_copy( case_input ); /* Case 1 */
  else if (file_type == ECL_EGRID_FILE)
    return util_alloc_string_copy( case_input ); /* Case 1 */
  else {
    char * grid_file = NULL;
    char * path;
    char * basename;
    util_alloc_file_components( case_input , &path , &basename , NULL);
    if ((file_type == ECL_OTHER_FILE) || (file_type == ECL_DATA_FILE)) {          /* Case 3 - only basename recognized */
      char * EGRID  = ecl_util_alloc_filename( path , basename , ECL_EGRID_FILE , false , -1);
      char * GRID   = ecl_util_alloc_filename( path , basename , ECL_GRID_FILE  , false , -1);
      char * FEGRID = ecl_util_alloc_filename( path , basename , ECL_EGRID_FILE , true  , -1);
      char * FGRID  = ecl_util_alloc_filename( path , basename , ECL_GRID_FILE  , true  , -1);

      if (util_file_exists( EGRID ))
        grid_file = util_alloc_string_copy( EGRID );
      else if (util_file_exists( GRID ))
        grid_file = util_alloc_string_copy( GRID );
      else if (util_file_exists( FEGRID ))
        grid_file = util_alloc_string_copy( FEGRID );
      else if (util_file_exists( FGRID ))
        grid_file = util_alloc_string_copy( FGRID );
      /*
        else: could not find a GRID/EGRID. 
      */

      free( EGRID );
      free( FEGRID );
      free( GRID );
      free( FGRID );
    } else {                                                                      /* Case 2 - we know the formatted / unformatted status. */
      char * EGRID  = ecl_util_alloc_filename( path , basename , ECL_EGRID_FILE , fmt_file , -1);
      char * GRID   = ecl_util_alloc_filename( path , basename , ECL_GRID_FILE  , fmt_file , -1);
      
      if (util_file_exists( EGRID ))
        grid_file = util_alloc_string_copy( EGRID );
      else if (util_file_exists( GRID ))
        grid_file = util_alloc_string_copy( GRID );
      
      free( EGRID );
      free( GRID );
    }
    return grid_file;
  }
}



ecl_grid_type * ecl_grid_load_case( const char * case_input ) {
  ecl_grid_type * ecl_grid = NULL;
  char * grid_file = ecl_grid_alloc_case_filename( case_input );
  if (grid_file != NULL) {
    ecl_grid = ecl_grid_alloc( grid_file );
    free( grid_file );
  }
  return ecl_grid;
}



bool ecl_grid_exists( const char * case_input ) {
  bool exists = false;
  char * grid_file = ecl_grid_alloc_case_filename( case_input );
  if (grid_file != NULL) {
    exists = true;
    free( grid_file );
  }
  return exists;
}



/**
   Return true if grids g1 and g2 are equal, and false otherwise. To
   return true all cells must be identical.
*/

bool ecl_grid_compare(const ecl_grid_type * g1 , const ecl_grid_type * g2) {
  int i;

  bool equal = true;
  if (g1->size != g2->size)
    equal = false;
  else {
    for (i = 0; i < g1->size; i++) {
      ecl_cell_type *c1 = g1->cells[i];
      ecl_cell_type *c2 = g2->cells[i];
      ecl_cell_compare(c1 , c2 , &equal);
    }
  }
  
  return equal;
}





/*****************************************************************/

bool ecl_grid_cell_contains_xyz1( const ecl_grid_type * ecl_grid , int global_index , double x , double y , double z) {
  point_type p;
  point_set( &p , x , y , z);
  
  return ecl_cell_contains_point( ecl_grid->cells[global_index] , &p);
}


bool ecl_grid_cell_contains_xyz3( const ecl_grid_type * ecl_grid , int i , int j , int k, double x , double y , double z) {
  int global_index = ecl_grid_get_global_index3( ecl_grid , i , j , k );
  return ecl_grid_cell_contains_xyz1( ecl_grid , global_index , x ,y  , z);
}



/**
   This function returns the global index for the cell (in layer 'k')
   which contains the point x,y. Observe that if you are looking for
   (i,j) you must call the function ecl_grid_get_ijk1() on the return value.
*/

int ecl_grid_get_global_index_from_xy( const ecl_grid_type * ecl_grid , int k , bool lower_layer , double x , double y) {

  int i,j;
  for (j=0; j < ecl_grid->ny; j++)
    for (i=0; i < ecl_grid->nx; i++) {
      int global_index = ecl_grid_get_global_index3( ecl_grid , i , j , k );
      if (ecl_cell_layer_contains_xy( ecl_grid->cells[ global_index ] , lower_layer , x , y))
        return global_index;  
    }
  return -1; /* Did not find x,y */
}



int ecl_grid_get_global_index_from_xy_top( const ecl_grid_type * ecl_grid , double x , double y) {
  return ecl_grid_get_global_index_from_xy( ecl_grid , ecl_grid->nz - 1 , false , x , y );
}

int ecl_grid_get_global_index_from_xy_bottom( const ecl_grid_type * ecl_grid , double x , double y) {
  return ecl_grid_get_global_index_from_xy( ecl_grid , 0 , true , x , y );
}


static void ecl_grid_clear_visited( ecl_grid_type * grid ) {
  if (grid->visited == NULL)
    grid->visited = util_malloc( sizeof * grid->visited * grid->size , __func__);

  for (int i=0; i < grid->size; i++)
    grid->visited[i] = false;
}


/* 
   Box coordinates are not inclusive, i.e. [i1,i2) 
*/
static int ecl_grid_box_contains_xyz( const ecl_grid_type * grid , int i1, int i2 , int j1 , int j2 , int k1 , int k2 , const point_type * p) {

  int i,j,k;
  int global_index = -1;
  for (k=k1; k < k2; k++)
    for (j=j1; j < j2; j++)
      for (i=i1; i < i2; i++) {
        global_index = ecl_grid_get_global_index3( grid , i , j , k);
        if (!grid->visited[ global_index ]) {
          grid->visited[ global_index ] = true;
          if (ecl_cell_contains_point( grid->cells[ global_index ] , p )) {
            return global_index;
          }
        }
      }
  return -1;  /* Returning -1; did not find xyz. */
}


/**
   This function will find the global index of the cell containing the
   world coordinates (x,y,z), if no cell can be found the function
   will return -1.

   The function is basically based on scanning through the cells in
   natural (i fastest) order and querying whether the cell[i,j,k]
   contains the (x,y,z) point; not very elegant :-(

   The last argument - 'start_index' - can be used to speed things up
   a bit if you have reasonable guess of where the the (x,y,z) is
   located. The start_index value is used as this:


     start_index == 0: I do not have a clue, start from the beginning
        and scan through the grid linearly.


     start_index != 0: 
        1. Check the cell 'start_index'.
        2. Check the neighbours (i +/- 1, j +/- 1, k +/- 1 ).
        3. Give up and do a linear search starting from start_index.

*/



int ecl_grid_get_global_index_from_xyz(ecl_grid_type * grid , double x , double y , double z , int start_index) {
  int global_index;
  point_type p;
  point_set( &p , x , y , z);
  ecl_grid_clear_visited( grid );
  
  if (start_index >= 0) {
    /* Try start index */
    if (ecl_cell_contains_point( grid->cells[start_index] , &p ))
      return start_index;
    else {
      /* Try neighbours */
      int i,j,k;
      int i1,i2,j1,j2,k1,k2;
      int nx,ny,nz;
      ecl_grid_get_dims( grid , &nx , &ny , &nz , NULL);
      ecl_grid_get_ijk1( grid , start_index , &i , &j , &k);

      i1 = util_int_max( 0 , i - 1 );
      j1 = util_int_max( 0 , j - 1 );
      k1 = util_int_max( 0 , k - 1 );
      
      i2 = util_int_min( nx , i + 1 );
      j2 = util_int_min( ny , j + 1 );
      k2 = util_int_min( nz , k + 1 );
      
      global_index = ecl_grid_box_contains_xyz( grid , i1 , i2 , j1 , j2 , k1 , k2 , &p);
      if (global_index >= 0)
        return global_index;


      /* Try a bigger box */
      i1 = util_int_max( 0 , i - 2 );
      j1 = util_int_max( 0 , j - 2 );
      k1 = util_int_max( 0 , k - 2 );
      
      i2 = util_int_min( nx , i + 2 );
      j2 = util_int_min( ny , j + 2 );
      k2 = util_int_min( nz , k + 2 );
      
      global_index = ecl_grid_box_contains_xyz( grid , i1 , i2 , j1 , j2 , k1 , k2 , &p);
      if (global_index >= 0)
        return global_index;


    }
  } 
  
  /* 
     OK - the attempted shortcuts did not pay off. We start on the
     full linear search starting from start_index.
  */
  
  {
    int index    = 0;
    global_index = -1;

    while (true) {
      int current_index = ((index + start_index) % grid->size);
      bool cell_contains;
      cell_contains = ecl_cell_contains_point( grid->cells[current_index] , &p );
      
      if (cell_contains) {
        global_index = current_index;
        break;
      }
      index++;
      if (index == grid->size)
        break;
    } 
  }
  return global_index;
}





static int ecl_grid_get_global_index_from_xy__(const ecl_grid_type * grid , double x , double y , int last_index) {
  util_exit("%s: not implemented ... \n");
  //int global_index;
  //ecl_point_type p;ordinates (
  //p.x = x;
  //p.y = y;
  //p.z = -1;
  //{
  //  int index    = 0;
  //  bool cont    = true;
  //  global_index = -1;
  //
  //  do {
  //    int active_index = ((index + last_index) % grid->block_size);
  //    bool cell_contains;
  //    cell_contains = ecl_cell_contains_2d(grid->cells[active_index] , p);
  //
  //    if (cell_contains) {
  //      global_index = active_index;
  //      cont = false;
  //    }
  //    index++;
  //    if (index == grid->block_size)
  //      cont = false;
  //  } while (cont);
  //}
  //return global_index;
  return -1;
}



void ecl_grid_alloc_blocking_variables(ecl_grid_type * grid, int block_dim) {
  int index;
  grid->block_dim = block_dim;
  if (block_dim == 2)
    grid->block_size = grid->nx* grid->ny;
  else if (block_dim == 3)
    grid->block_size = grid->size;
  else
    util_abort("%: valid values are two and three. Value:%d invaid \n",__func__ , block_dim);

  grid->values         = util_malloc( grid->block_size * sizeof * grid->values , __func__);
  for (index = 0; index < grid->block_size; index++)
    grid->values[index] = double_vector_alloc( 0 , 0.0 );
}



void ecl_grid_init_blocking(ecl_grid_type * grid) {
  int index;
  for (index = 0; index < grid->block_size; index++)
    double_vector_reset(grid->values[index]);
  grid->last_block_index = 0;
}




bool ecl_grid_block_value_3d(ecl_grid_type * grid, double x , double y , double z , double value) {
  if (grid->block_dim != 3)
    util_abort("%s: Wrong blocking dimension \n",__func__);
  {
    int global_index = ecl_grid_get_global_index_from_xyz( grid , x , y , z , grid->last_block_index);
    if (global_index >= 0) {
      double_vector_append( grid->values[global_index] , value);
      grid->last_block_index = global_index;
      return true;
    } else
      return false;
  }
}



bool ecl_grid_block_value_2d(ecl_grid_type * grid, double x , double y ,double value) {
  if (grid->block_dim != 2)
    util_abort("%s: Wrong blocking dimension \n",__func__);
  {
    int global_index = ecl_grid_get_global_index_from_xy__( grid , x , y , grid->last_block_index);
    if (global_index >= 0) {
      double_vector_append( grid->values[global_index] , value);
      grid->last_block_index = global_index;
      return true;
    } else
      return false;
  }
}



double ecl_grid_block_eval2d(ecl_grid_type * grid , int i, int j , block_function_ftype * blockf ) {
  int global_index = ecl_grid_get_global_index3(grid , i,j,0);
  return blockf( grid->values[global_index]);
}


double ecl_grid_block_eval3d(ecl_grid_type * grid , int i, int j , int k ,block_function_ftype * blockf ) {
  int global_index = ecl_grid_get_global_index3(grid , i,j,k);
  return blockf( grid->values[global_index]);
}

int ecl_grid_get_block_count2d(const ecl_grid_type * grid , int i , int j) {
  int global_index = ecl_grid_get_global_index3(grid , i,j,0);
  return double_vector_size( grid->values[global_index]);
}


int ecl_grid_get_block_count3d(const ecl_grid_type * grid , int i , int j, int k) {
  int global_index = ecl_grid_get_global_index3(grid , i,j,k);
  return double_vector_size( grid->values[global_index]);
}

/* End of blocking functions                                     */
/*****************************************************************/

void ecl_grid_free(ecl_grid_type * grid) {
  int i;
  for (i=0; i < grid->size; i++)
    ecl_cell_free(grid->cells[i]);
  free(grid->cells);
  util_safe_free(grid->index_map);
  util_safe_free(grid->inv_index_map);

  if (grid->values != NULL) {
    int i;
    for (i=0; i < grid->block_size; i++)
      double_vector_free( grid->values[i] );
    free( grid->values );
  }
  if (grid->grid_nr == 0) { /* This is the main grid. */
    vector_free( grid->LGR_list );
    hash_free( grid->LGR_hash );
  }
  hash_free( grid->children );
  util_safe_free( grid->parent_name );
  util_safe_free( grid->visited );
  util_safe_free( grid->name );
  free( grid );
}


void ecl_grid_free__( void * arg ) {
  ecl_grid_type * ecl_grid = ecl_grid_safe_cast( arg );
  ecl_grid_free( ecl_grid );
}




void ecl_grid_get_distance(const ecl_grid_type * grid , int global_index1, int global_index2 , double *dx , double *dy , double *dz) {
  const ecl_cell_type * cell1 = grid->cells[global_index1];
  const ecl_cell_type * cell2 = grid->cells[global_index2];
  
  *dx = cell1->center->x - cell2->center->x;
  *dy = cell1->center->y - cell2->center->y;
  *dz = cell1->center->z - cell2->center->z;

}



/*****************************************************************/
/* Index based query functions */
/*****************************************************************/



/**
   Only checks that i,j,k are in the required intervals:
  
      0 <= i < nx
      0 <= j < ny
      0 <= k < nz

*/
   
bool ecl_grid_ijk_valid(const ecl_grid_type * grid , int i , int j , int k) {
  bool OK = false;

  if (i >= 0 && i < grid->nx)
    if (j >= 0 && j < grid->ny)
      if (k >= 0 && k < grid->nz)
        OK = true;

  return OK;
}


void ecl_grid_get_dims(const ecl_grid_type * grid , int *nx , int * ny , int * nz , int * active_size) {
  if (nx != NULL) *nx                   = grid->nx;
  if (ny != NULL) *ny                   = grid->ny;
  if (nz != NULL) *nz                   = grid->nz;
  if (active_size != NULL) *active_size = grid->total_active;
}

int ecl_grid_get_nz( const ecl_grid_type * grid ) {
  return grid->nz;
}

int ecl_grid_get_nx( const ecl_grid_type * grid ) {
  return grid->nx;
}

int ecl_grid_get_ny( const ecl_grid_type * grid ) {
  return grid->ny;
}

int ecl_grid_get_parent_cell1( const ecl_grid_type * grid , int global_index ) {
  return grid->cells[global_index]->host_cell;
}


int ecl_grid_get_parent_cell3( const ecl_grid_type * grid , int i , int j , int k) {
  int global_index = ecl_grid_get_global_index__(grid , i , j , k);
  return ecl_grid_get_parent_cell1( grid , global_index );
}


/*****************************************************************/
/* Functions for converting between the different index types. */

/**
   Converts: (i,j,k) -> global_index. i,j,k are zero offset.
*/

int ecl_grid_get_global_index3(const ecl_grid_type * ecl_grid , int i , int j , int k) {
  if (ecl_grid_ijk_valid(ecl_grid , i , j , k))
    return ecl_grid_get_global_index__(ecl_grid , i , j , k);
  else {
    util_abort("%s: i,j,k = (%d,%d,%d) is invalid:\n\n  nx: [0,%d>\n  ny: [0,%d>\n  nz: [0,%d>\n",__func__ , i,j,k,ecl_grid->nx,ecl_grid->ny,ecl_grid->nz);
    return -1; /* Compiler shut up. */
  }
}


/**
   Converts: active_index -> global_index
*/

int ecl_grid_get_global_index1A(const ecl_grid_type * ecl_grid , int active_index) {
  return ecl_grid->inv_index_map[active_index];
}



/**
   Converts: (i,j,k) -> active_index
   (i,j,k ) are zero offset.
   
   Will return -1 if the cell is not active.
*/

int ecl_grid_get_active_index3(const ecl_grid_type * ecl_grid , int i , int j , int k) {
  int global_index = ecl_grid_get_global_index3(ecl_grid , i,j,k);  /* In range: [0,nx*ny*nz) */
  return ecl_grid_get_active_index1(ecl_grid , global_index);
}


/**
   Converts: global_index -> active_index.
   
   Will return -1 if the cell is not active.
*/

int ecl_grid_get_active_index1(const ecl_grid_type * ecl_grid , int global_index) {
  return ecl_grid->index_map[global_index];
}


/*
  Converts global_index -> (i,j,k)
  
  This function returns C-based zero offset indices. cell_
*/

void ecl_grid_get_ijk1(const ecl_grid_type * grid , int global_index, int *i, int *j , int *k) {
  *k = global_index / (grid->nx * grid->ny); global_index -= (*k) * (grid->nx * grid->ny);
  *j = global_index / grid->nx;              global_index -= (*j) *  grid->nx;
  *i = global_index;
}

/*
  Converts active_index -> (i,j,k)
*/

void ecl_grid_get_ijk1A(const ecl_grid_type *ecl_grid , int active_index , int *i, int * j, int * k) {
  if (active_index >= 0 && active_index < ecl_grid->total_active) {
    int global_index = ecl_grid_get_global_index1A( ecl_grid , active_index );
    ecl_grid_get_ijk1(ecl_grid , global_index , i,j,k);
  } else
    util_abort("%s: error active_index:%d invalid - grid has only:%d active cells. \n",__func__ , active_index , ecl_grid->total_active);
}


/******************************************************************/
/*
  Functions to get the 'true' (i.e. UTM or whatever) position (x,y,z).
*/

/*
  ijk are C-based zero offset.
*/

void ecl_grid_get_xyz1(const ecl_grid_type * grid , int global_index , double *xpos , double *ypos , double *zpos) {
  const ecl_cell_type * cell = grid->cells[global_index];
  *xpos = cell->center->x;
  *ypos = cell->center->y;
  *zpos = cell->center->z;
}



void ecl_grid_get_xyz3(const ecl_grid_type * grid , int i, int j , int k, double *xpos , double *ypos , double *zpos) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , k );
  ecl_grid_get_xyz1( grid , global_index , xpos , ypos , zpos);
}





/**
   This function will return (by reference) the x,y,z values of corner
   nr 'corner_nr' in cell 'global_index'. See the documentation of
   tetraheder decomposition for the numbering of the corners.
*/


void ecl_grid_get_corner_xyz1(const ecl_grid_type * grid , int global_index , int corner_nr , double * xpos , double * ypos , double * zpos ) {
  if ((corner_nr >= 0) &&  (corner_nr <= 7)) {
    const ecl_cell_type * cell  = grid->cells[ global_index ];
    const point_type    * point = cell->corner_list[ corner_nr ];
    *xpos = point->x;
    *ypos = point->y;
    *zpos = point->z;
  }
}


void ecl_grid_get_corner_xyz3(const ecl_grid_type * grid , int i , int j , int k, int corner_nr , double * xpos , double * ypos , double * zpos ) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , k );
  ecl_grid_get_corner_xyz1( grid , global_index , corner_nr , xpos , ypos , zpos);
}



void ecl_grid_get_xyz1A(const ecl_grid_type * grid , int active_index , double *xpos , double *ypos , double *zpos) {
  const int global_index = ecl_grid_get_global_index1A( grid , active_index );
  ecl_grid_get_xyz1( grid , global_index , xpos , ypos , zpos );
}



double ecl_grid_get_cdepth1(const ecl_grid_type * grid , int global_index) {
  const ecl_cell_type * cell = grid->cells[global_index];
  return cell->center->z;
}


double ecl_grid_get_cdepth3(const ecl_grid_type * grid , int i, int j , int k) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , k );
  return ecl_grid_get_cdepth1( grid , global_index );
}


int ecl_grid_locate_depth( const ecl_grid_type * grid , double depth , int i , int j ) {
  if (depth < ecl_grid_get_top2( grid , i , j))
    return -1;
  else if (depth >= ecl_grid_get_bottom2( grid , i , j ))
    return -1 * grid->nz;
  else {
    int k=0;
    double bottom = ecl_grid_get_top3( grid , i , j , k);

    while (true) {
      double top = bottom;
      bottom = ecl_grid_get_bottom3( grid , i , j , k );

      if ((depth >= top) && (depth < bottom)) 
        return k;
      
      k++;
      if (k == grid->nz)
        util_abort("%s: internal error when scanning for depth:%g \n",__func__ , depth);
    }
  }
}



/**
   Returns the depth of the top surface of the cell. 
*/

double ecl_grid_get_top1(const ecl_grid_type * grid , int global_index) {
  const ecl_cell_type * cell = grid->cells[global_index];
  double depth = 0;
  for (int ij = 0; ij < 4; ij++) 
    depth += cell->corner_list[ij]->z;
  
  return depth * 0.25;
}



double ecl_grid_get_top3(const ecl_grid_type * grid , int i, int j , int k) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , k );
  return ecl_grid_get_top1( grid , global_index );
}


double ecl_grid_get_top2(const ecl_grid_type * grid , int i, int j) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , 0);
  return ecl_grid_get_top1( grid , global_index );
}


double ecl_grid_get_bottom2(const ecl_grid_type * grid , int i, int j) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , grid->nz - 1);
  return ecl_grid_get_bottom1( grid , global_index );
}



double ecl_grid_get_top1A(const ecl_grid_type * grid , int active_index) {
  const int global_index = ecl_grid_get_global_index1A(grid , active_index);
  return ecl_grid_get_top1( grid , global_index );
}


/**
   Returns the depth of the bottom surface of the cell. 
*/

double ecl_grid_get_bottom1(const ecl_grid_type * grid , int global_index) {
  const ecl_cell_type * cell = grid->cells[global_index];
  double depth = 0;
  for (int ij = 0; ij < 4; ij++) 
    depth += cell->corner_list[ij + 4]->z;
  
  return depth * 0.25;
}


double ecl_grid_get_bottom3(const ecl_grid_type * grid , int i, int j , int k) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , k );
  return ecl_grid_get_bottom1( grid , global_index );
}



double ecl_grid_get_bottom1A(const ecl_grid_type * grid , int active_index) {
  const int global_index = ecl_grid_get_global_index1A(grid , active_index);
  return ecl_grid_get_bottom1( grid , global_index );
}


  
double ecl_grid_get_cell_thickness1( const ecl_grid_type * grid , int global_index ) {
  const ecl_cell_type * cell = grid->cells[global_index];
  double thickness = 0;
  for (int ij = 0; ij < 4; ij++) 
    thickness += (cell->corner_list[ij + 4]->z - cell->corner_list[ij]->z);
  
  return thickness * 0.25;
}


double ecl_grid_get_cell_thickness3( const ecl_grid_type * grid , int i , int j , int k) {
  const int global_index = ecl_grid_get_global_index3(grid , i,j,k);
  return ecl_grid_get_cell_thickness1( grid , global_index );
}


/*****************************************************************/
/* Functions to query whether a cell is active or not.           */

/*
   Global index in [0,...,nx*ny*nz)
*/

bool ecl_grid_cell_active1(const ecl_grid_type * ecl_grid , int global_index) {
  if (ecl_grid->index_map[global_index] >= 0)
    return true;
  else
    return false;
}



bool ecl_grid_cell_active3(const ecl_grid_type * ecl_grid, int i , int j , int k) {
  int global_index = ecl_grid_get_global_index3( ecl_grid , i , j , k);
  return ecl_grid_cell_active1( ecl_grid , global_index );
}


/*****************************************************************/
/* Functions for LGR query/lookup/... */

static void __assert_main_grid(const ecl_grid_type * ecl_grid) {
  if (ecl_grid->grid_nr != 0) 
    util_abort("%s: tried to get LGR grid from another LGR_grid - only main grid can be used as first input \n",__func__);
}


/**
   This functon will return a a ecl_grid instance corresponding to the
   lgr with name lgr_name. The function will fail HARD if no lgr with
   this name is installed under the present main grid; check first
   with ecl_grid_has_lgr() if you are whimp.
   
   Leading/trailing spaces on lgr_name are stripped prior to the hash lookup.
*/


ecl_grid_type * ecl_grid_get_lgr(const ecl_grid_type * main_grid, const char * __lgr_name) {
  __assert_main_grid( main_grid );
  {
    char * lgr_name          = util_alloc_strip_copy( __lgr_name );
    ecl_grid_type * lgr_grid = hash_get(main_grid->LGR_hash , lgr_name);
    free(lgr_name);
    return lgr_grid;
  }
}


/**
   Returns true/false if the main grid has a a lgr with name
   __lgr_name. Leading/trailing spaces are stripped before checking.
*/

bool ecl_grid_has_lgr(const ecl_grid_type * main_grid, const char * __lgr_name) {
  __assert_main_grid( main_grid );
  {
    char * lgr_name          = util_alloc_strip_copy( __lgr_name );
    bool has_lgr             = hash_has_key( main_grid->LGR_hash , lgr_name );
    free(lgr_name);
    return has_lgr;
  }
}


/**
   Return the number of LGR's associated with this main grid
   instance. The main grid is not counted.
*/
int ecl_grid_get_num_lgr(const ecl_grid_type * main_grid ) {
  __assert_main_grid( main_grid );
  return vector_get_size( main_grid->LGR_list ) - 1;  
}

/**
   The lgr_nr has zero offset, not counting the main grid, i.e.

      ecl_grid_iget_lgr( ecl_grid , 0);
   
   will return the first LGR - and fail HARD if there are no LGR's.
*/

ecl_grid_type * ecl_grid_iget_lgr(const ecl_grid_type * main_grid, int lgr_nr) {
  __assert_main_grid( main_grid );
  return vector_iget(  main_grid->LGR_list , lgr_nr + 1);
}


/**
   The following functions will return the LGR subgrid referenced by
   the coordinates given. Observe the following:

   1. The functions will happily return NULL if no LGR is associated
      with the cell indicated - in fact that is (currently) the only
      way to query whether a particular cell has a LGR.
      
   2. If a certain cell is refined in several levels this function
      will return a pointer to the first level of refinement. The
      return value can can be used for repeated calls to descend
      deeper into the refinement hierarchy.  
*/


const ecl_grid_type * ecl_grid_get_cell_lgr1(const ecl_grid_type * grid , int global_index ) {
  const ecl_cell_type * cell = grid->cells[global_index];
  return cell->lgr;
}


const ecl_grid_type * ecl_grid_get_cell_lgr3(const ecl_grid_type * grid , int i, int j , int k) {
  const int global_index = ecl_grid_get_global_index__(grid , i , j , k );
  return ecl_grid_get_cell_lgr1( grid , global_index );
}



const ecl_grid_type * ecl_grid_get_cell_lgr1A(const ecl_grid_type * grid , int active_index) {
  const int global_index = ecl_grid_get_global_index1A( grid , active_index );
  return ecl_grid_get_cell_lgr1( grid , global_index );
}


/**
   Will return the global grid for a lgr. If the input grid is indeed
   a global grid itself the function will return NULL.
*/
const ecl_grid_type * ecl_grid_get_global_grid( const ecl_grid_type * grid ) {
  return grid->global_grid;
}


/*****************************************************************/

/** 
    Allocates a stringlist instance with the lookup names of the lgr names in this grid.
*/

stringlist_type * ecl_grid_alloc_lgr_name_list(const ecl_grid_type * ecl_grid) {
  __assert_main_grid( ecl_grid );
  {
    return hash_alloc_stringlist( ecl_grid->LGR_hash );
  }
}



/*****************************************************************/

/**
   This function returns the grid_nr field of the field; this is just
   the occurence number in the grid file. Starting with 0 at the main
   grid, and then increasing consecutively through the lgr sections.

   Observe that there is A MAJOR POTENTIAL for confusion with the
   ecl_grid_iget_lgr() function, the latter does not refer to the main
   grid and returns the first lgr section (which has grid_nr == 1) for
   input argument 0.
*/


int ecl_grid_get_grid_nr( const ecl_grid_type * ecl_grid ) { 
  return ecl_grid->grid_nr; 
}


const char * ecl_grid_get_name( const ecl_grid_type * ecl_grid ) {
  return ecl_grid->name;
}


int ecl_grid_get_global_size( const ecl_grid_type * ecl_grid ) {
  return ecl_grid->nx * ecl_grid->ny * ecl_grid->nz;
}

int ecl_grid_get_active_size( const ecl_grid_type * ecl_grid ) {
  return ecl_grid->total_active;
}

double ecl_grid_get_cell_volume1( const ecl_grid_type * ecl_grid, int global_index ) {
  const ecl_cell_type * cell = ecl_grid->cells[global_index];
  return ecl_cell_get_volume( cell );
}


double ecl_grid_get_cell_volume3( const ecl_grid_type * ecl_grid, int i , int j , int k) {
  int global_index = ecl_grid_get_global_index3( ecl_grid , i , j , k);
  return ecl_grid_get_cell_volume1( ecl_grid , global_index );
}


void ecl_grid_summarize(const ecl_grid_type * ecl_grid) {
  int             active_cells , nx,ny,nz;
  ecl_grid_get_dims(ecl_grid , &nx , &ny , &nz , &active_cells);
  printf("      Name ............: %s  \n",ecl_grid->name);
  printf("      Active cells ....: %d \n",active_cells);
  printf("      nx ..............: %d \n",nx);
  printf("      ny ..............: %d \n",ny);
  printf("      nz ..............: %d \n",nz);
  printf("      Volume ..........: %d \n",nx*ny*nz);
  printf("      Origo X..........: %10.2f \n",ecl_grid->origo[0]);
  printf("      Origo Y..........: %10.2f \n",ecl_grid->origo[1]);


  if (ecl_grid->grid_nr == 0) {
    for (int grid_nr=1; grid_nr < vector_get_size( ecl_grid->LGR_list ); grid_nr++) {
      printf("\n");
      ecl_grid_summarize( vector_iget_const( ecl_grid->LGR_list , grid_nr ));
    }
  }
}

/*****************************************************************/
/**
   
   This function is used to translate (with the help of the ecl_grid
   functionality) i,j,k to an index which can be used to look up an
   element in the ecl_kw instance. It is just a minor convenience
   function.

   * If the ecl_kw instance has nx*ny*nz (i,j,k) are translated to a
     global index with ecl_grid_get_global_index3(). This is typically
     the case when the ecl_kw instance represents a petrophysical
     property which is e.g. loaded from a INIT file.

   * If the ecl_kw instance has nactive elements the (i,j,k) indices
     are converted to an active index with
     ecl_grid_get_active_index3(). This is typically the case if the
     ecl_kw instance is a solution vector which has been loaded from a
     restart file. If you ask for an inactive cell the function will
     return -1.

   * If the ecl_kw instance has neither nx*ny*nz nor nactive elements
     the function will fail HARD.

   * The return value is double, irrespective of the type of the
     underlying datatype of the ecl_kw instance - the function will
     fail HARD if the underlying type can not be safely converted to
     double, i.e. if it is not in the set [ecl_float_type ,
     ecl_int_type , ecl_double_type].

   * i,j,k: C-based zero offset grid coordinates.

*/


double ecl_grid_get_property(const ecl_grid_type * ecl_grid , const ecl_kw_type * ecl_kw , int i , int j , int k) {
  ecl_type_enum ecl_type = ecl_kw_get_type( ecl_kw );
  if ((ecl_type == ECL_FLOAT_TYPE) || (ecl_type == ECL_INT_TYPE) || (ecl_type == ECL_DOUBLE_TYPE)) {
    int kw_size        = ecl_kw_get_size( ecl_kw );
    int lookup_index   = -1;

    if (kw_size == ecl_grid->nx * ecl_grid->ny * ecl_grid->nz) 
      lookup_index = ecl_grid_get_global_index3(ecl_grid , i , j , k);
    else if (kw_size == ecl_grid->total_active) 
      /* Will be set to -1 if the cell is not active. */ 
      lookup_index = ecl_grid_get_active_index3(ecl_grid , i , j , k);
    else 
      util_abort("%s: incommensurable size ... \n",__func__);

    if (lookup_index >= 0)
      return ecl_kw_iget_as_double( ecl_kw , lookup_index );
    else
      return -1;   /* Tried to lookup an inactive cell. */
  } else {
    util_abort("%s: sorry - can not lookup ECLIPSE type:%s with %s.\n",__func__ , ecl_util_get_type_name( ecl_type ) , __func__);
    return -1;
  }
}


/**
   Will fill the double_vector instance @column with values from
   ecl_kw from the column given by (i,j). If @ecl_kw has size nactive
   the inactive k values will not be set, i.e. you should make sure
   that the default value of the @column instance has been properly
   set beforehand.

   The column vector will be filled with double values, the content of
   ecl_kw will be converted to double in the case INTE,REAL and DOUB
   types, otherwsie it is crash and burn.
*/


void ecl_grid_get_column_property(const ecl_grid_type * ecl_grid , const ecl_kw_type * ecl_kw , int i , int j, double_vector_type * column) {
  ecl_type_enum ecl_type = ecl_kw_get_type( ecl_kw );
  if ((ecl_type == ECL_FLOAT_TYPE) || (ecl_type == ECL_INT_TYPE) || (ecl_type == ECL_DOUBLE_TYPE)) {
    int kw_size        = ecl_kw_get_size( ecl_kw );
    bool use_global_index = false;

    if (kw_size == ecl_grid->nx * ecl_grid->ny * ecl_grid->nz) 
      use_global_index = true;
    else if (kw_size == ecl_grid->total_active) 
      use_global_index = false;
    else 
      util_abort("%s: incommensurable sizes: nx*ny*nz = %d  nactive=%d  kw_size:%d \n",__func__ , ecl_grid->size , ecl_grid->total_active , ecl_kw_get_size( ecl_kw ));

    double_vector_reset( column );
    {
      for (int k=0; k < ecl_grid->nz; k++) {
        if (use_global_index) {
          int global_index = ecl_grid_get_global_index3( ecl_grid , i , j , k );
          double_vector_iset( column , k , ecl_kw_iget_as_double( ecl_kw , global_index ));
        } else {
          int active_index = ecl_grid_get_active_index3( ecl_grid , i , j , k );
          if (active_index >= 0)
            double_vector_iset( column, k , ecl_kw_iget_as_double( ecl_kw , active_index ));
        }
      }
    }
  } else 
    util_abort("%s: sorry - can not lookup ECLIPSE type:%s with %s.\n",__func__ , ecl_util_get_type_name( ecl_type ) , __func__);
}


/*****************************************************************/
/**
   This function will look up all the indices in the grid where the
   region_kw has a certain value (region_value). The ecl_kw instance
   must be loaded beforehand, typically with the functions
   ecl_kw_grdecl_fseek_kw / ecl_kw_fscanf_alloc_grdecl_data.

   The two boolean flags active_only and export_active_index determine
   how active/inactive indieces should be handled:

     active_only: Means that only cells which match the required
        region_value AND are also active are stored. If active_only is
        set to false, ALL cells matching region value are stored in
        index_list.

     export_active_index: if this value is true the the index of the
        cell is in the space of active cells, otherwise it is in terms
        of the global indexing.

   Observe the following about the ecl_kw instance wth region data:

    * It must be of type integer - otherwise we blow up hard.  The
    * size must be the total number of cells (should handle boxes and
      so on ...)

   Observe that there is no way to get ijk from this function, then
   you must call ecl_grid_get_ijk() afterwards. the return value is
   the number of cells found.
*/

int ecl_grid_get_region_cells(const ecl_grid_type * ecl_grid , const ecl_kw_type * region_kw , int region_value , bool active_only, bool export_active_index , int_vector_type * index_list) {
  int cells_found = 0;
  if (ecl_kw_get_size( region_kw ) == ecl_grid->size) {
    if (ecl_kw_get_type( region_kw ) == ECL_INT_TYPE) {
      int_vector_reset( index_list );
      const int * region_ptr = ecl_kw_iget_ptr( region_kw , 0);

      {
        int global_index;
        for (global_index = 0; global_index < ecl_grid->size; global_index++) {
          if (region_ptr[global_index] == region_value) {
             if (!active_only || (ecl_grid->index_map[global_index] >= 0)) {
              /* Okay - this index should be included */
              if (export_active_index)
                int_vector_iset(index_list , cells_found , ecl_grid->index_map[global_index]);
              else
                int_vector_iset(index_list , cells_found , global_index);
              cells_found++;
            }
           }
        }
      }
    }  else
      util_abort("%s: type mismatch - regions_kw must be of type integer \n",__func__);

  } else
    util_abort("%s: size mismatch grid has %d cells - region specifier:%d \n",__func__ , ecl_grid->size , ecl_kw_get_size( region_kw ));
  return cells_found;
}



/*****************************************************************/



void ecl_grid_grdecl_fprintf_kw( const ecl_grid_type * ecl_grid , const ecl_kw_type * ecl_kw , FILE * stream , double double_default) {
  int src_size = ecl_kw_get_size( ecl_kw );
  if (src_size == ecl_grid->size)
    ecl_kw_fprintf_grdecl( ecl_kw , stream );
  else if (src_size == ecl_grid->total_active) {
    void  * default_ptr = NULL;
    float   float_default;
    int     int_default;
    int     bool_default;
    ecl_type_enum ecl_type = ecl_kw_get_type( ecl_kw );
    
    if (ecl_type == ECL_FLOAT_TYPE) {
      float_default = (float) double_default;
      default_ptr = &float_default;
    } else if (ecl_type == ECL_INT_TYPE) {
      int_default = (int) double_default;
      default_ptr = &int_default;
    } else if (ecl_type == ECL_DOUBLE_TYPE) {
      default_ptr = &double_default;
    } else if (ecl_type == ECL_BOOL_TYPE) {
      int tmp = (int) double_default;
      if (tmp == 1)
        bool_default = ECL_BOOL_TRUE_INT;
      else if (tmp == 0)
        bool_default = ECL_BOOL_FALSE_INT;
      else
        util_abort("%s: only 0 and 1 are allowed for bool interpolation\n",__func__);
      default_ptr = &bool_default;
    }
    
    if (default_ptr == NULL) 
      util_abort("%s: invalid type \n",__func__);
    
    {
      ecl_kw_type * tmp_kw = ecl_kw_alloc_scatter_copy( ecl_kw , ecl_grid->size , ecl_grid->inv_index_map , default_ptr );
      ecl_kw_fprintf_grdecl( tmp_kw , stream );
      ecl_kw_free( tmp_kw );
    }
  } else 
    util_abort("%s: size mismatch. ecl_kw must have either nx*ny*ny elements or nactive elements\n",__func__);

}

