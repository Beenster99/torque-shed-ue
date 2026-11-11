cmake_minimum_required(VERSION 3.16)
project(torque_sim_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(SIM ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/Sim)
add_executable(sim_test sim_test.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp ${SIM}/BikeSim.cpp ${SIM}/BikeContacts.cpp ${SIM}/BikeHeat.cpp)
if(MSVC)
  target_compile_options(sim_test PRIVATE /W4)
else()
  target_compile_options(sim_test PRIVATE -Wall -Wextra -Wshadow=local -Werror=shadow=local)
endif()

set(SOUND ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/TorqueAudio)
add_executable(sound_test sound_test.cpp ${SOUND}/RideSound.cpp ${SOUND}/EngineSynth.cpp)
target_include_directories(sound_test PRIVATE ${SOUND})
if(NOT MSVC)
  target_compile_options(sound_test PRIVATE -Wall -Wextra -Wshadow=local -Werror=shadow=local)
endif()

set(COOP ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/Coop)
add_executable(net_test net_test.cpp)
target_include_directories(net_test PRIVATE ${COOP})
target_compile_definitions(net_test PRIVATE TORQUE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto")
if(NOT MSVC)
  target_compile_options(net_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow)
endif()

# The fitment system: assembles the Speed Twin from its sockets and runs the clearance checks.
set(FIT ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/Fit)
add_executable(fit_test fit_test.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/Fitment.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp)
target_include_directories(fit_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(fit_test PRIVATE /W4)
else()
  target_compile_options(fit_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# The layout solver (the Godot game's BikeRig.solve): lays out every style's reference bike.
add_executable(layout_test layout_test.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${FIT}/BikeBody.cpp ${FIT}/BikeParts.cpp ${FIT}/BikeFrame.cpp ${FIT}/PartMesh.cpp)
target_include_directories(layout_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(layout_test PRIVATE /W4)
else()
  target_compile_options(layout_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# Opening the router's port (UPnP): the search, the router's description and the SOAP messages.
add_executable(upnp_test upnp_test.cpp)
target_include_directories(upnp_test PRIVATE ${COOP})
if(NOT MSVC)
  target_compile_options(upnp_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow)
endif()

# Geometry mode's fit (the Godot game's GeoGizmo and apply_geometry, plus finer control): checked
# against the real game's answers, then applied to every reference bike.
add_executable(geo_test geo_test.cpp ${FIT}/BikeGeometry.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${FIT}/BikeBody.cpp ${FIT}/BikeParts.cpp ${FIT}/BikeFrame.cpp ${FIT}/PartMesh.cpp)
target_include_directories(geo_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(geo_test PRIVATE /W4)
else()
  target_compile_options(geo_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# How a bike of catalogue parts rides (BikeBuild): checked against the real game's numbers.
add_executable(spec_test spec_test.cpp ${FIT}/BikeRideSpec.cpp ${FIT}/BikeTune.cpp ${SIM}/BikeSim.cpp ${SIM}/BikeContacts.cpp ${SIM}/BikeHeat.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp)
target_include_directories(spec_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(spec_test PRIVATE /W4)
else()
  target_compile_options(spec_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# The dyno desk's tune (BikeBuild's tune rules and what a tune does): checked against the real game's.
add_executable(tune_test tune_test.cpp ${FIT}/BikeTune.cpp ${FIT}/BikeRideSpec.cpp ${SIM}/BikeSim.cpp ${SIM}/BikeContacts.cpp ${SIM}/BikeHeat.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp)
target_include_directories(tune_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(tune_test PRIVATE /W4)
else()
  target_compile_options(tune_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# The shed's sounds (Torque Shed's Sfx): the manifest and takes in Data/Audio/TS, the rules, the mixer.
add_executable(sfx_test sfx_test.cpp ${SOUND}/SfxMix.cpp ${SOUND}/SfxBank.cpp ${FIT}/FitJson.cpp)
target_include_directories(sfx_test PRIVATE ${SOUND} ${FIT})
target_compile_definitions(sfx_test PRIVATE TORQUE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../Data")
if(MSVC)
  target_compile_options(sfx_test PRIVATE /W4)
else()
  target_compile_options(sfx_test PRIVATE -Wall -Wextra -Wshadow=local -Werror=shadow=local)
endif()

# The New bike configurator (the Godot game's BikeShop): checked against the real game's answers.
add_executable(shop_test shop_test.cpp ${FIT}/BikeShop.cpp ${FIT}/BikeRideSpec.cpp ${FIT}/BikeTune.cpp ${SIM}/BikeSim.cpp ${SIM}/BikeContacts.cpp ${SIM}/BikeHeat.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp)
target_include_directories(shop_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(shop_test PRIVATE /W4)
else()
  target_compile_options(shop_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# The tank badge (the Godot game's BikeBadge): the logos' pictures, where the badge goes on the
# tank and the decal skins cut from it, checked against the real game's.
add_executable(badge_test badge_test.cpp ${FIT}/BikeBadge.cpp ${FIT}/BikeShop.cpp ${FIT}/BikeRideSpec.cpp ${FIT}/BikeTune.cpp ${SIM}/BikeSim.cpp ${SIM}/BikeContacts.cpp ${SIM}/BikeHeat.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp)
target_include_directories(badge_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(badge_test PRIVATE /W4)
else()
  target_compile_options(badge_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# Torque Shed's engine sound (EngineSynth): every kind of engine baked and ridden.
add_executable(engine_test engine_test.cpp ${SOUND}/EngineSynth.cpp ${SOUND}/RideSound.cpp)
target_include_directories(engine_test PRIVATE ${SOUND})
target_compile_definitions(engine_test PRIVATE TORQUE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../Data")
if(MSVC)
  target_compile_options(engine_test PRIVATE /W4)
else()
  target_compile_options(engine_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow)
endif()

# Every style's stock bike ridden against the real bikes it stands for (0-60, 60-0, full-lean turn).
add_executable(realism_test realism_test.cpp ${FIT}/BikeRideSpec.cpp ${FIT}/BikeTune.cpp ${SIM}/BikeSim.cpp ${SIM}/BikeContacts.cpp ${SIM}/BikeHeat.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp ${SIM}/TyreModel.cpp ${SIM}/BikeSpec.cpp)
target_include_directories(realism_test PRIVATE ${FIT} ${SIM})
if(MSVC)
  target_compile_options(realism_test PRIVATE /W4)
else()
  target_compile_options(realism_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# The weather engine (live weather): presets, fades, roads that remember rain, and the live feeds' answers.
set(WEATHER ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/Weather)
add_executable(weather_test weather_test.cpp ${WEATHER}/WeatherModel.cpp ${FIT}/FitJson.cpp)
target_include_directories(weather_test PRIVATE ${WEATHER} ${FIT})
if(MSVC)
  target_compile_options(weather_test PRIVATE /W4)
else()
  target_compile_options(weather_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# Reading the real world (Ride UK stage 1): the grid, the height tiles, cells stitched into a road
# graph and the ground's height, against the Godot game's answers on the same files.
set(WORLD ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/World)
add_executable(world_test world_test.cpp ${WORLD}/WorldGeo.cpp ${WORLD}/WorldFiles.cpp ${WORLD}/WorldSha256.cpp ${WORLD}/WorldInflate.cpp
  ${WORLD}/WorldImage.cpp ${WORLD}/HeightTiles.cpp ${WORLD}/WorldRegion.cpp ${WORLD}/WorldTerrain.cpp ${WORLD}/WorldZip.cpp ${WORLD}/CellStore.cpp
  ${WORLD}/WorldLoad.cpp ${FIT}/FitJson.cpp)
target_include_directories(world_test PRIVATE ${WORLD} ${FIT})
target_compile_definitions(world_test PRIVATE TORQUE_WORLD_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data/world" TORQUE_WORLD_TMP="${CMAKE_CURRENT_BINARY_DIR}")
find_package(Threads REQUIRED)
target_link_libraries(world_test PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(world_test PRIVATE /W4)
else()
  target_compile_options(world_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()
# The same check the game runs with -TorqueWorldTest, from a desk with the real network (it shells out to curl).
if(NOT WIN32)
  add_executable(world_ride world_ride.cpp ${WORLD}/WorldGeo.cpp ${WORLD}/WorldFiles.cpp ${WORLD}/WorldSha256.cpp ${WORLD}/WorldInflate.cpp
    ${WORLD}/WorldImage.cpp ${WORLD}/HeightTiles.cpp ${WORLD}/WorldRegion.cpp ${WORLD}/WorldTerrain.cpp ${WORLD}/WorldZip.cpp ${WORLD}/CellStore.cpp
    ${WORLD}/WorldLoad.cpp ${FIT}/FitJson.cpp)
  target_include_directories(world_ride PRIVATE ${WORLD} ${FIT})
  target_link_libraries(world_ride PRIVATE Threads::Threads)
  target_compile_options(world_ride PRIVATE -Wall -Wextra -Wshadow -Werror=shadow -Wconversion -Wno-sign-conversion)
endif()

# The shed radio's stream decoder (Icecast MP3 + song titles, minimp3), and dropping stations quickly.
add_executable(radio_test radio_test.cpp ${SOUND}/RadioStream.cpp)
target_include_directories(radio_test PRIVATE ${SOUND})
target_compile_definitions(radio_test PRIVATE TORQUE_SIMTEST="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(radio_test PRIVATE /W4)
else()
  target_compile_options(radio_test PRIVATE -Wall -Wextra -Wshadow -Werror=shadow)
endif()
