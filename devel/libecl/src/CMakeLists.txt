include_directories( ext )
file(GLOB ext_source "ext/*.c" )
file(GLOB ext_header "ext/*.h" )

set( source_files ecl_rsthead.c ecl_sum_tstep.c ecl_rst_file.c ecl_init_file.c ecl_grid_cache.c smspec_node.c ecl_kw_grdecl.c ecl_file_kw.c ecl_grav.c ecl_grav_calc.c ecl_smspec.c ecl_sum_data.c ecl_util.c ecl_kw.c ecl_sum.c fortio.c ecl_rft_file.c ecl_rft_node.c ecl_rft_cell.c ecl_grid.c ecl_coarse_cell.c ecl_box.c ecl_io_config.c ecl_file.c ecl_region.c point.c tetrahedron.c ecl_subsidence.c ecl_grid_dims.c grid_dims.c nnc_info.c ecl_grav_common.c nnc_vector.c nnc_index_list.c ${ext_source})

set( header_files ecl_rsthead.h ecl_sum_tstep.h ecl_rst_file.h ecl_init_file.h smspec_node.h ecl_grid_cache.h ecl_kw_grdecl.h ecl_file_kw.h ecl_grav.h ecl_grav_calc.h ecl_endian_flip.h ecl_smspec.h ecl_sum_data.h ecl_util.h ecl_kw.h ecl_sum.h fortio.h ecl_rft_file.h ecl_rft_node.h ecl_rft_cell.h ecl_box.h ecl_coarse_cell.h ecl_grid.h ecl_io_config.h ecl_file.h ecl_region.h ecl_kw_magic.h ecl_subsidence.h ecl_grid_dims.h grid_dims.h nnc_info.h nnc_vector.h ${ext_header} ecl_grav_common.h nnc_index_list.h)



add_library( ecl ${LIBRARY_TYPE} ${source_files} )
set_target_properties( ecl PROPERTIES VERSION 1.0 SOVERSION 1.0 )
if (USE_RUNPATH)
   add_runpath( ecl )
endif()

#-----------------------------------------------------------------
set( OPENMP OFF )
option(USE_OPENMP "Include OpenMP support" OFF)
if (USE_OPENMP)
   INCLUDE(CheckCCompilerFlag)
   CHECK_C_COMPILER_FLAG(-fopenmp OPENMP)
endif()

if (OPENMP)
   add_definitions( -DHAVE_OPENMP -fopenmp )
   target_link_libraries( ecl gomp )
else()
   add_definitions( -fno-openmp )        
endif()
target_link_libraries( ecl ert_geometry ert_util )

#-----------------------------------------------------------------
if (INSTALL_ERT) 
    install(TARGETS ecl DESTINATION ${CMAKE_INSTALL_LIBDIR})
    foreach(header ${header_files})
        install(FILES ../include/ert/ecl/${header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/ert/ecl)
    endforeach()
endif()




