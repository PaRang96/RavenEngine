cmake_minimum_required(VERSION 3.28)
project(SamplesGame LANGUAGES CXX)
include("${RAVEN_ROOT}/cmake/RavenGame.cmake")
raven_add_game(Samples)