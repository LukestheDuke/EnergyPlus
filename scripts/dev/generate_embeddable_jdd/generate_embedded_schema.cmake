execute_process( COMMAND "${EnergyPlus_BINARY_DIR}/scripts/generate_embeddedable_schema" "${EnergyPlus_RUNTIME_OUTPUT_DIRECTORY}/Energy+.jdd" TIMEOUT 30 RESULT_VARIABLE generate_embedded_schema OUTPUT_VARIABLE embedded_schema)
if( ${generate_embedded_schema} MATCHES ".*timeout.*" )
  message(FATAL_ERROR "Generating embedded JDD from JDD failed: ${generate_embedded_schema}")
endif()
configure_file( "${EnergyPlus_SOURCE_DIR}/InputProcessing/EmbeddedJDD.in.cc" "${EnergyPlus_CURRENT_BINARY_DIR}/EmbeddedJDD.cc" )