target_sources(SRB2SDL2 PRIVATE
	hw_bsp.cpp
	hw_draw.cpp
	hw_light.cpp
	hw_main.cpp
	hw_clip.cpp
	hw_md2.cpp
	hw_cache.cpp
	hw_md2load.cpp
	hw_md3load.cpp
	hw_model.cpp
	u_list.cpp
	hw_batching.cpp
	r_opengl/r_opengl.cpp
	# Ring Racers E
	rr_staticgeo.cpp
	rr_gfxapi.cpp
	rr_cfgsync.cpp
)

# Ring Racers E: Direct3D 9 / 9Ex RHI backend (Windows only).
if(WIN32)
	target_sources(SRB2SDL2 PRIVATE
		../rhi/d3d9/d3d9_rhi.cpp
		../rhi/d3d9/d3d9_shaders.cpp
		../rhi/d3d9/d3dcompiler_embed.cpp
	)

	# Optionally embed d3dcompiler_47.dll inside the executable (Windows 7 does not ship it).
	# Put d3dcompiler_47_x64.dll / d3dcompiler_47_x86.dll (from the Windows SDK redist) in
	# RRE_EMBED_DIR. If the file is missing, nothing is embedded and the game loads the
	# system DLL at runtime instead.
	set(RRE_EMBED_DIR "${CMAKE_SOURCE_DIR}/embed" CACHE PATH "Folder with d3dcompiler_47_x64/x86.dll to embed")
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(RRE_D3DC_FILE "${RRE_EMBED_DIR}/d3dcompiler_47_x64.dll")
	else()
		set(RRE_D3DC_FILE "${RRE_EMBED_DIR}/d3dcompiler_47_x86.dll")
	endif()
	if(EXISTS "${RRE_D3DC_FILE}")
		message(STATUS "Embedding d3dcompiler: ${RRE_D3DC_FILE}")
		set_source_files_properties(../rhi/d3d9/d3dcompiler_embed.cpp TARGET_DIRECTORY SRB2SDL2 PROPERTIES
			COMPILE_DEFINITIONS "RR_D3DC_PATH=${RRE_D3DC_FILE}" OBJECT_DEPENDS "${RRE_D3DC_FILE}")
	else()
		message(STATUS "d3dcompiler_47 not embedded (not found in ${RRE_EMBED_DIR})")
	endif()
endif()
