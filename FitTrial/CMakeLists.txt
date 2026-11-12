# The bike fitting trial: a program of its own, apart from the game. It reads the game's parts and
# fitting code (read only) and writes pictures and reports. Nothing here is built into the game.
cmake_minimum_required(VERSION 3.16)
project(FitTrial CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(FIT ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/Fit)
set(SIM ${CMAKE_CURRENT_SOURCE_DIR}/../Source/TorqueShedProto/Sim)
find_package(Threads REQUIRED)
add_executable(fit_trial trial.cpp ${FIT}/BikeGeometry.cpp ${FIT}/FitJson.cpp ${FIT}/PartsDb.cpp ${FIT}/BikeLayout.cpp rules/BikeBody.cpp
  rules/BikeParts.cpp rules/BikeFrame.cpp ${FIT}/PartMesh.cpp)
target_include_directories(fit_trial PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rules ${FIT} ${SIM})
target_link_libraries(fit_trial PRIVATE Threads::Threads)
target_compile_options(fit_trial PRIVATE -O2 -Wall -Wextra)
