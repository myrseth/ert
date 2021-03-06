add_executable( enkf_runpath_list enkf_runpath_list.c )
target_link_libraries( enkf_runpath_list enkf test_util )

add_executable( enkf_site_config enkf_site_config.c )
target_link_libraries( enkf_site_config enkf test_util )

add_executable( enkf_time_map enkf_time_map.c )
target_link_libraries( enkf_time_map enkf test_util )

add_executable( enkf_pca_plot enkf_pca_plot.c )
target_link_libraries( enkf_pca_plot enkf test_util)
add_test( enkf_pca_plot ${EXECUTABLE_OUTPUT_PATH}/enkf_pca_plot)

add_executable( enkf_cases_config enkf_cases_config.c )
target_link_libraries( enkf_cases_config enkf test_util )
add_test( enkf_cases_config  ${EXECUTABLE_OUTPUT_PATH}/enkf_cases_config )

add_executable( enkf_analysis_config enkf_analysis_config.c )
target_link_libraries( enkf_analysis_config enkf test_util )
add_test( enkf_analysis  ${EXECUTABLE_OUTPUT_PATH}/enkf_analysis_config )

add_executable( enkf_state_map enkf_state_map.c )
target_link_libraries( enkf_state_map enkf test_util )

add_executable( enkf_meas_data enkf_meas_data.c )
target_link_libraries( enkf_meas_data enkf test_util )

add_executable( enkf_ensemble_GEN_PARAM enkf_ensemble_GEN_PARAM.c )
target_link_libraries( enkf_ensemble_GEN_PARAM enkf test_util )

add_executable( enkf_ensemble enkf_ensemble.c )
target_link_libraries( enkf_ensemble enkf test_util )

add_executable( enkf_local_obsdata_node enkf_local_obsdata_node.c )
target_link_libraries( enkf_local_obsdata_node enkf test_util)
add_test( enkf_local_obsdata_node ${EXECUTABLE_OUTPUT_PATH}/enkf_local_obsdata_node )

add_executable( enkf_local_obsdata enkf_local_obsdata.c )
target_link_libraries( enkf_local_obsdata enkf test_util)
add_test( enkf_local_obsdata ${EXECUTABLE_OUTPUT_PATH}/enkf_local_obsdata )

add_executable( enkf_active_list enkf_active_list.c )
target_link_libraries( enkf_active_list enkf test_util)
add_test( enkf_active_list ${EXECUTABLE_OUTPUT_PATH}/enkf_active_list )

add_executable( enkf_obs_tstep_list enkf_obs_tstep_list.c )
target_link_libraries( enkf_obs_tstep_list enkf test_util)
add_test( enkf_obs_tstep_list ${EXECUTABLE_OUTPUT_PATH}/enkf_obs_tstep_list )

add_executable( enkf_main enkf_main.c )
target_link_libraries( enkf_main enkf test_util )
add_test( enkf_main  ${EXECUTABLE_OUTPUT_PATH}/enkf_main )

#-----------------------------------------------------------------

add_executable( enkf_forward_init_GEN_KW enkf_forward_init_GEN_KW.c )
target_link_libraries( enkf_forward_init_GEN_KW enkf test_util )

add_test( enkf_forward_init_GEN_KW_TRUE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_GEN_KW 
          ${CMAKE_CURRENT_SOURCE_DIR}/data/config/forward/ert config_GEN_KW_true TRUE)

add_test( enkf_forward_init_GEN_KW_FALSE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_GEN_KW 
          ${CMAKE_CURRENT_SOURCE_DIR}/data/config/forward/ert config_GEN_KW_false FALSE)


#-----------------------------------------------------------------

add_executable( enkf_state_report_step_compatible enkf_state_report_step_compatible.c )
target_link_libraries( enkf_state_report_step_compatible enkf test_util )

add_test( enkf_state_report_step_compatible_TRUE 
          ${EXECUTABLE_OUTPUT_PATH}/enkf_state_report_step_compatible ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/ecl_sum_compatible_true  config_ecl_sum_compatible_true TRUE)

add_test( enkf_state_report_step_compatible_FALSE 
          ${EXECUTABLE_OUTPUT_PATH}/enkf_state_report_step_compatible ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/ecl_sum_compatible_false  config_ecl_sum_compatible_false FALSE)
                                                                      

#-----------------------------------------------------------------

add_executable( enkf_forward_init_GEN_PARAM enkf_forward_init_GEN_PARAM.c )
target_link_libraries( enkf_forward_init_GEN_PARAM enkf test_util )

add_test( enkf_forward_init_GEN_PARAM_TRUE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_GEN_PARAM 
          ${CMAKE_CURRENT_SOURCE_DIR}/data/config/forward/ert config_GEN_PARAM_true TRUE)

add_test( enkf_forward_init_GEN_PARAM_FALSE  
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_GEN_PARAM 
          ${CMAKE_CURRENT_SOURCE_DIR}/data/config/forward/ert config_GEN_PARAM_false FALSE)

#-----------------------------------------------------------------

add_executable( enkf_forward_init_SURFACE enkf_forward_init_SURFACE.c )
target_link_libraries( enkf_forward_init_SURFACE enkf test_util )

add_test( enkf_forward_init_SURFACE_TRUE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_SURFACE 
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/surface config_surface_true   
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/surface/Surface.irap
          TRUE)

add_test( enkf_forward_init_SURFACE_FALSE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_SURFACE 
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/surface config_surface_false
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/surface/Surface.irap
          FALSE)

#-----------------------------------------------------------------

add_executable( enkf_forward_init_FIELD enkf_forward_init_FIELD.c )
target_link_libraries( enkf_forward_init_FIELD enkf test_util )

add_test( enkf_forward_init_FIELD_TRUE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_FIELD 
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/field config_field_true   
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/field/petro.grdecl
          TRUE)

add_test( enkf_forward_init_FIELD_FALSE   
          ${EXECUTABLE_OUTPUT_PATH}/enkf_forward_init_FIELD 
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/field config_field_false
          ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/forward_init/field/petro.grdecl
          FALSE)

#-----------------------------------------------------------------


add_executable( enkf_iter_config enkf_iter_config.c )
target_link_libraries( enkf_iter_config enkf test_util )
add_test( enkf_iter_config  ${EXECUTABLE_OUTPUT_PATH}/enkf_iter_config )

add_executable( enkf_model_config enkf_model_config.c )
target_link_libraries( enkf_model_config enkf test_util )
add_test( enkf_model_config  ${EXECUTABLE_OUTPUT_PATH}/enkf_model_config )

add_executable( enkf_rng enkf_rng.c )
target_link_libraries( enkf_rng enkf test_util )
add_test( enkf_rng  ${EXECUTABLE_OUTPUT_PATH}/enkf_rng ${CMAKE_CURRENT_SOURCE_DIR}/data/config rng)

add_executable( enkf_report_list enkf_report_list.c )
target_link_libraries( enkf_report_list enkf test_util )
add_test( enkf_report_list  ${EXECUTABLE_OUTPUT_PATH}/enkf_report_list ${CMAKE_CURRENT_SOURCE_DIR}/data/config/ert_report_list)

add_executable( enkf_refcase_list enkf_refcase_list.c )
target_link_libraries( enkf_refcase_list enkf test_util )
add_test( enkf_refcase_list  ${EXECUTABLE_OUTPUT_PATH}/enkf_refcase_list ${PROJECT_SOURCE_DIR}/test-data/Statoil/ECLIPSE/Gurbat/ECLIPSE ${PROJECT_SOURCE_DIR}/test-data/Statoil/ECLIPSE/Gurbat*/ECLIPSE) 
add_test( enkf_refcase_list2  ${EXECUTABLE_OUTPUT_PATH}/enkf_refcase_list ${PROJECT_SOURCE_DIR}/test-data/Statoil/ECLIPSE/Gurbat/ECLIPSE ${PROJECT_SOURCE_DIR}/test-data/Statoil/ECLIPSE/Gurbat*/ECLIPSE.*) 
set_property( TEST enkf_refcase_list PROPERTY LABELS StatoilData )
set_property( TEST enkf_refcase_list2 PROPERTY LABELS StatoilData )

add_executable( enkf_ecl_config enkf_ecl_config.c )
target_link_libraries( enkf_ecl_config enkf test_util )
add_test( enkf_ecl_config1  ${EXECUTABLE_OUTPUT_PATH}/enkf_ecl_config )
add_test( enkf_ecl_config2  ${EXECUTABLE_OUTPUT_PATH}/enkf_ecl_config ${PROJECT_SOURCE_DIR}/test-data/Statoil/ECLIPSE/Gurbat/ECLIPSE)
set_property( TEST enkf_ecl_config2 PROPERTY LABELS StatoilData )

add_executable( enkf_ecl_config_config enkf_ecl_config_config.c )
target_link_libraries( enkf_ecl_config_config enkf test_util )
add_test( enkf_ecl_config_config  ${EXECUTABLE_OUTPUT_PATH}/enkf_ecl_config_config ${PROJECT_SOURCE_DIR}/test-data/Statoil/config/ecl_config )
set_property( TEST enkf_ecl_config_config PROPERTY LABELS StatoilData )

add_executable( enkf_qc_module_test enkf_qc_module_test.c )
target_link_libraries( enkf_qc_module_test enkf test_util )
add_test( enkf_qc_module_test ${EXECUTABLE_OUTPUT_PATH}/enkf_qc_module_test )
 
add_executable(enkf_obs_tests enkf_obs_tests.c)
target_link_libraries(enkf_obs_tests enkf test_util )
add_test(enkf_obs_tests ${EXECUTABLE_OUTPUT_PATH}/enkf_obs_tests)

add_executable(obs_vector_tests obs_vector_tests.c)
target_link_libraries(obs_vector_tests enkf test_util )
add_test(obs_vector_tests ${EXECUTABLE_OUTPUT_PATH}/obs_vector_tests)

add_test( enkf_runpath_list  ${EXECUTABLE_OUTPUT_PATH}/enkf_runpath_list )
add_test( enkf_site_config  ${EXECUTABLE_OUTPUT_PATH}/enkf_site_config /project/res/etc/ERT/site-config)
add_test( enkf_time_map1  ${EXECUTABLE_OUTPUT_PATH}/enkf_time_map )
add_test( enkf_time_map2  ${EXECUTABLE_OUTPUT_PATH}/enkf_time_map ${PROJECT_SOURCE_DIR}/test-data/Statoil/ECLIPSE/Gurbat/ECLIPSE.UNRST )
add_test( enkf_ensemble_GEN_PARAM  ${EXECUTABLE_OUTPUT_PATH}/enkf_ensemble_GEN_PARAM ${CMAKE_CURRENT_SOURCE_DIR}/data/ensemble/GEN_PARAM )
add_test( enkf_ensemble  ${EXECUTABLE_OUTPUT_PATH}/enkf_ensemble )
add_test( enkf_state_map  ${EXECUTABLE_OUTPUT_PATH}/enkf_state_map )
add_test( enkf_meas_data  ${EXECUTABLE_OUTPUT_PATH}/enkf_meas_data )

set_property( TEST enkf_time_map2     PROPERTY LABELS StatoilData )
set_property( TEST enkf_site_config   PROPERTY LABELS StatoilData )
set_property( TEST enkf_state_report_step_compatible_TRUE  PROPERTY LABELS StatoilData )
set_property( TEST enkf_state_report_step_compatible_FALSE  PROPERTY LABELS StatoilData )
set_property( TEST enkf_forward_init_SURFACE_FALSE  PROPERTY LABELS StatoilData )
set_property( TEST enkf_forward_init_SURFACE_TRUE   PROPERTY LABELS StatoilData )
set_property( TEST enkf_forward_init_FIELD_FALSE  PROPERTY LABELS StatoilData )
set_property( TEST enkf_forward_init_FIELD_TRUE   PROPERTY LABELS StatoilData )

