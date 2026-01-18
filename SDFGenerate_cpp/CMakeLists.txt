cmake_minimum_required(VERSION 3.16)
  project(SDFGenerate_cpp LANGUAGES C CXX)

  set(CMAKE_CXX_STANDARD 11)

  add_executable(SDFGenerate
      SDFGenerate.cpp
      lodepng.cpp
  )
  target_compile_definitions(SDFGenerate PRIVATE _CRT_SECURE_NO_WARNINGS)

  add_executable(GenerateSelf
      GenerateSelf.cpp
      lodepng.cpp
  )

  # 可选：OpenMP（#pragma omp for 会在有 OpenMP 时生效）
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(SDFGenerate PRIVATE OpenMP::OpenMP_CXX)
  endif()