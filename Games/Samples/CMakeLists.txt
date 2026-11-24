cmake_minimum_required(VERSION 3.24)
# Must precede project(): it decides whether the debug format comes from the target property.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
project(SamplesGame LANGUAGES CXX)
include("${RAVEN_ROOT}/cmake/RavenGame.cmake")
raven_add_game(Samples)